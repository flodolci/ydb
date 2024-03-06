#include "base_status_updater_actor.h"

#include <ydb/core/fq/libs/common/util.h>
#include <ydb/core/fq/libs/compute/common/metrics.h>
#include <ydb/core/fq/libs/compute/common/retry_actor.h>
#include <ydb/core/fq/libs/compute/common/run_actor_params.h>
#include <ydb/core/fq/libs/compute/ydb/events/events.h>
#include <ydb/core/fq/libs/compute/ydb/control_plane/compute_database_control_plane_service.h>
#include <ydb/core/fq/libs/ydb/ydb.h>
#include <ydb/core/util/backoff.h>
#include <ydb/library/services/services.pb.h>

#include <ydb/library/yql/dq/actors/dq.h>
#include <ydb/library/yql/providers/common/metrics/service_counters.h>

#include <ydb/public/sdk/cpp/client/ydb_query/client.h>
#include <ydb/public/sdk/cpp/client/ydb_operation/operation.h>

#include <ydb/library/actors/core/actor.h>
#include <ydb/library/actors/core/actor_bootstrapped.h>
#include <ydb/library/actors/core/actorsystem.h>
#include <ydb/library/actors/core/hfunc.h>
#include <ydb/library/actors/core/log.h>


#define LOG_E(stream) LOG_ERROR_S(*TlsActivationContext, NKikimrServices::FQ_RUN_ACTOR, "[ydb] [StatusTracker] QueryId: " << Params.QueryId << " OperationId: " << ProtoToString(OperationId) << " " << stream)
#define LOG_W(stream) LOG_WARN_S( *TlsActivationContext, NKikimrServices::FQ_RUN_ACTOR, "[ydb] [StatusTracker] QueryId: " << Params.QueryId << " OperationId: " << ProtoToString(OperationId) << " " << stream)
#define LOG_I(stream) LOG_INFO_S( *TlsActivationContext, NKikimrServices::FQ_RUN_ACTOR, "[ydb] [StatusTracker] QueryId: " << Params.QueryId << " OperationId: " << ProtoToString(OperationId) << " " << stream)
#define LOG_D(stream) LOG_DEBUG_S(*TlsActivationContext, NKikimrServices::FQ_RUN_ACTOR, "[ydb] [StatusTracker] QueryId: " << Params.QueryId << " OperationId: " << ProtoToString(OperationId) << " " << stream)
#define LOG_T(stream) LOG_TRACE_S(*TlsActivationContext, NKikimrServices::FQ_RUN_ACTOR, "[ydb] [StatusTracker] QueryId: " << Params.QueryId << " OperationId: " << ProtoToString(OperationId) << " " << stream)

namespace NFq {

using namespace NActors;
using namespace NFq;

class TStatusTrackerActor : public TBaseStatusUpdaterActor<TStatusTrackerActor> {
public:
    using IRetryPolicy = IRetryPolicy<const TEvYdbCompute::TEvGetOperationResponse::TPtr&>;

    enum ERequestType {
        RT_GET_OPERATION,
        RT_PING,
        RT_MAX
    };

    class TCounters: public virtual TThrRefBase {
        std::array<TComputeRequestCountersPtr, RT_MAX> Requests = CreateArray<RT_MAX, TComputeRequestCountersPtr>({
            { MakeIntrusive<TComputeRequestCounters>("GetOperation") },
            { MakeIntrusive<TComputeRequestCounters>("Ping") }
        });

        ::NMonitoring::TDynamicCounterPtr Counters;

    public:
        explicit TCounters(const ::NMonitoring::TDynamicCounterPtr& counters)
            : Counters(counters)
        {
            for (auto& request: Requests) {
                request->Register(Counters);
            }
        }

        TComputeRequestCountersPtr GetCounters(ERequestType type) {
            return Requests[type];
        }
    };

    TStatusTrackerActor(const TRunActorParams& params, const TActorId& parent, const TActorId& connector, const TActorId& pinger, const NYdb::TOperation::TOperationId& operationId, const ::NYql::NCommon::TServiceCounters& queryCounters)
        : TBaseStatusUpdaterActor(params.Config.GetCommon(), queryCounters, "StatusTracker")
        , Params(params)
        , Parent(parent)
        , Connector(connector)
        , Pinger(pinger)
        , OperationId(operationId)
        , Counters(GetStepCountersSubgroup())
        , BackoffTimer(20, 1000)
    {
        SetPingCounters(Counters.GetCounters(ERequestType::RT_PING));
    }

    static constexpr char ActorName[] = "FQ_STATUS_TRACKER";

    void Start() {
        LOG_I("Become");
        Become(&TStatusTrackerActor::StateFunc);
        SendGetOperation();
    }

    STRICT_STFUNC(StateFunc,
        hFunc(TEvYdbCompute::TEvGetOperationResponse, Handle);
        hFunc(TEvents::TEvForwardPingResponse, Handle);
    )

    void Handle(const TEvents::TEvForwardPingResponse::TPtr& ev) {
        OnPingRequestFinish(ev.Get()->Get()->Success);

        if (ev->Cookie) {
            return;
        }

        if (ev.Get()->Get()->Success) {
            LOG_I("Information about the status of operation is stored");
            Send(Parent, new TEvYdbCompute::TEvStatusTrackerResponse(Issues, Status, ExecStatus, ComputeStatus));
            CompleteAndPassAway();
        } else {
            LOG_E("Error saving information about the status of operation");
            Send(Parent, new TEvYdbCompute::TEvStatusTrackerResponse(NYql::TIssues{NYql::TIssue{TStringBuilder{} << "Error saving information about the status of operation: " << ProtoToString(OperationId)}}, NYdb::EStatus::INTERNAL_ERROR, ExecStatus, ComputeStatus));
            FailedAndPassAway();
        }
    }

    void Handle(const TEvYdbCompute::TEvGetOperationResponse::TPtr& ev) {
        const auto& response = *ev.Get()->Get();

        if (response.Status == NYdb::EStatus::SUCCESS && !response.Ready) {
            LOG_D("GetOperation IS NOT READY, repeating");
            SendGetOperation(TDuration::MilliSeconds(BackoffTimer.NextBackoffMs()));
            return;
        }

        if (response.Status == NYdb::EStatus::NOT_FOUND) { // FAILING / ABORTING_BY_USER / ABORTING_BY_SYSTEM
            LOG_I("Operation has been already removed");
            Send(Parent, new TEvYdbCompute::TEvStatusTrackerResponse(response.Issues, response.Status, ExecStatus, ComputeStatus));
            CompleteAndPassAway();
            return;
        }

        if (response.Status != NYdb::EStatus::SUCCESS) {
            LOG_E("Can't get operation: " << response.Issues.ToOneLineString());
            Send(Parent, new TEvYdbCompute::TEvStatusTrackerResponse(response.Issues, response.Status, ExecStatus, ComputeStatus));
            FailedAndPassAway();
            return;
        }

        ReportPublicCounters(response.QueryStats);
        LOG_D("Execution status: " << static_cast<int>(response.ExecStatus));
        switch (response.ExecStatus) {
            case NYdb::NQuery::EExecStatus::Unspecified:
            case NYdb::NQuery::EExecStatus::Starting:
                SendGetOperation(TDuration::MilliSeconds(BackoffTimer.NextBackoffMs()));
                QueryStats = response.QueryStats;
                UpdateProgress();
                break;
            case NYdb::NQuery::EExecStatus::Aborted:
            case NYdb::NQuery::EExecStatus::Canceled:
            case NYdb::NQuery::EExecStatus::Failed:
                Issues = response.Issues;
                Status = response.Status;
                ExecStatus = response.ExecStatus;
                QueryStats = response.QueryStats;
                StatusCode = NYql::NDq::YdbStatusToDqStatus(response.StatusCode);
                Failed();
                break;
            case NYdb::NQuery::EExecStatus::Completed:
                Issues = response.Issues;
                Status = response.Status;
                ExecStatus = response.ExecStatus;
                QueryStats = response.QueryStats;
                Complete();
                break;
        }
    }

    void ReportPublicCounters(const Ydb::TableStats::QueryStats& stats) {
        try {
            auto stat = GetPublicStat(GetV1StatFromV2Plan(stats.query_plan()));
            auto publicCounters = GetPublicCounters();

            if (stat.MemoryUsageBytes) {
                auto& counter = *publicCounters->GetNamedCounter("name", "query.memory_usage_bytes");
                counter = *stat.MemoryUsageBytes;
            }

            if (stat.CpuUsageUs) {
                auto& counter = *publicCounters->GetNamedCounter("name", "query.cpu_usage_us", true);
                counter = *stat.CpuUsageUs;
            }

            if (stat.InputBytes) {
                auto& counter = *publicCounters->GetNamedCounter("name", "query.input_bytes", true);
                counter = *stat.InputBytes;
            }

            if (stat.OutputBytes) {
                auto& counter = *publicCounters->GetNamedCounter("name", "query.output_bytes", true);
                counter = *stat.OutputBytes;
            }

            if (stat.SourceInputRecords) {
                auto& counter = *publicCounters->GetNamedCounter("name", "query.source_input_records", true);
                counter = *stat.SourceInputRecords;
            }

            if (stat.SinkOutputRecords) {
                auto& counter = *publicCounters->GetNamedCounter("name", "query.sink_output_records", true);
                counter = *stat.SinkOutputRecords;
            }

            if (stat.RunningTasks) {
                auto& counter = *publicCounters->GetNamedCounter("name", "query.running_tasks");
                counter = *stat.RunningTasks;
            }
        } catch(const NJson::TJsonException& ex) {
            LOG_E("Error statistics conversion: " << ex.what());
        }
    }

    void SendGetOperation(const TDuration& delay = TDuration::Zero()) {
        Register(new TRetryActor<TEvYdbCompute::TEvGetOperationRequest, TEvYdbCompute::TEvGetOperationResponse, NYdb::TOperation::TOperationId>(Counters.GetCounters(ERequestType::RT_GET_OPERATION), delay, SelfId(), Connector, OperationId));
    }

    std::pair<Fq::Private::PingTaskRequest, double> GetPingTaskRequestWithStatistic(std::optional<FederatedQuery::QueryMeta::ComputeStatus> computeStatus, std::optional<NYql::NDqProto::StatusIds::StatusCode> pendingStatusCode) {
        Fq::Private::PingTaskRequest pingTaskRequest;
        double cpuUsage = 0.0;
        try {
            pingTaskRequest = GetPingTaskRequestStatistics(computeStatus, pendingStatusCode, Issues, QueryStats, &cpuUsage);
        } catch(const NJson::TJsonException& ex) {
            LOG_E("Error statistics conversion: " << ex.what());
        }

        return { pingTaskRequest, cpuUsage };
    }

    void UpdateProgress() {
        OnPingRequestStart();

        Fq::Private::PingTaskRequest pingTaskRequest = GetPingTaskRequestWithStatistic(std::nullopt, std::nullopt).first;
        Send(Pinger, new TEvents::TEvForwardPingRequest(pingTaskRequest), 0, 1);
    }

    void UpdateCpuQuota(double cpuUsage) {
        TDuration duration = TDuration::MicroSeconds(QueryStats.total_duration_us());
        if (cpuUsage && duration) {
            Send(NFq::ComputeDatabaseControlPlaneServiceActorId(), new TEvYdbCompute::TEvCpuQuotaAdjust(Params.Scope.ToString(), duration, cpuUsage)); 
        }
    }

    void Failed() {
        LOG_I("Execution status: Failed, Status: " << Status << ", StatusCode: " << NYql::NDqProto::StatusIds::StatusCode_Name(StatusCode) << " Issues: " << Issues.ToOneLineString());
        OnPingRequestStart();

        auto [pingTaskRequest, cpuUsage] = GetPingTaskRequestWithStatistic(std::nullopt, StatusCode);
        UpdateCpuQuota(cpuUsage);

        Send(Pinger, new TEvents::TEvForwardPingRequest(pingTaskRequest));
    }

    void Complete() {
        LOG_I("Execution status: Complete " << Status << ", StatusCode: " << NYql::NDqProto::StatusIds::StatusCode_Name(StatusCode) << " Issues: " << Issues.ToOneLineString());
        OnPingRequestStart();

        ComputeStatus = ::FederatedQuery::QueryMeta::COMPLETING;
        auto [pingTaskRequest, cpuUsage] = GetPingTaskRequestWithStatistic(ComputeStatus, std::nullopt);
        UpdateCpuQuota(cpuUsage);

        Send(Pinger, new TEvents::TEvForwardPingRequest(pingTaskRequest));
    }

private:
    TRunActorParams Params;
    TActorId Parent;
    TActorId Connector;
    TActorId Pinger;
    NYdb::TOperation::TOperationId OperationId;
    TCounters Counters;
    NYql::TIssues Issues;
    NYdb::EStatus Status = NYdb::EStatus::SUCCESS;
    NYdb::NQuery::EExecStatus ExecStatus = NYdb::NQuery::EExecStatus::Unspecified;
    NYql::NDqProto::StatusIds::StatusCode StatusCode = NYql::NDqProto::StatusIds::StatusCode::StatusIds_StatusCode_UNSPECIFIED;
    Ydb::TableStats::QueryStats QueryStats;
    NKikimr::TBackoffTimer BackoffTimer;
    FederatedQuery::QueryMeta::ComputeStatus ComputeStatus = FederatedQuery::QueryMeta::RUNNING;
};

std::unique_ptr<NActors::IActor> CreateStatusTrackerActor(const TRunActorParams& params,
                                                          const TActorId& parent,
                                                          const TActorId& connector,
                                                          const TActorId& pinger,
                                                          const NYdb::TOperation::TOperationId& operationId,
                                                          const ::NYql::NCommon::TServiceCounters& queryCounters) {
    return std::make_unique<TStatusTrackerActor>(params, parent, connector, pinger, operationId, queryCounters);
}

}