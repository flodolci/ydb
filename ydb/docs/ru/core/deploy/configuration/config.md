# Статическая конфигурация кластера

Статическая конфигурация кластера задается в YAML-файле, передаваемом в параметре `--yaml-config` при запуске узлов кластера.

В статье приведено описание основных групп конфигурируемых параметров в данном файле.

## host_configs — типовые конфигурации хостов {#host-configs}

Кластер YDB состоит из множества узлов, для развертывания которых обычно используется одна или несколько типовых конфигураций серверов. Для того чтобы не повторять её описание для каждого узла, в файле конфигурации существует раздел `host_configs`, в котором перечислены используемые конфигурации, и им присвоены идентификаторы.

**Синтаксис**

``` yaml
host_configs:
- host_config_id: 1
  drive:
  - path: <path_to_device>
    type: <type>
  - path: ...
- host_config_id: 2
  ...
```

Атрибут `host_config_id` задает числовой идентификатор конфигурации. В атрибуте `drive` содержится коллекция описаний подключенных дисков. Каждое описание состоит из двух атрибутов:

- `path` : Путь к смонтированному блочному устройству, например `/dev/disk/by-partlabel/ydb_disk_ssd_01`
- `type` : Тип физического носителя устройства: `ssd`, `nvme` или `rot` (rotational - HDD)

**Примеры**

Одна конфигурация с идентификатором 1, с одним диском типа SSD, доступным по пути `/dev/disk/by-partlabel/ydb_disk_ssd_01`:

``` yaml
host_configs:
- host_config_id: 1
  drive:
  - path: /dev/disk/by-partlabel/ydb_disk_ssd_01
    type: SSD
```

Две конфигурации с идентификаторами 1 (два SSD диска) и 2 (три SSD диска):

``` yaml
host_configs:
- host_config_id: 1
  drive:
  - path: /dev/disk/by-partlabel/ydb_disk_ssd_01
    type: SSD
  - path: /dev/disk/by-partlabel/ydb_disk_ssd_02
    type: SSD
- host_config_id: 2
  drive:
  - path: /dev/disk/by-partlabel/ydb_disk_ssd_01
    type: SSD
  - path: /dev/disk/by-partlabel/ydb_disk_ssd_02
    type: SSD
  - path: /dev/disk/by-partlabel/ydb_disk_ssd_03
    type: SSD
```

### Особенности Kubernetes {#host-configs-k8s}

YDB Kubernetes operator монтирует NBS диски для Storage узлов на путь `/dev/kikimr_ssd_00`. Для их использования должна быть указана следующая конфигурация `host_configs`:

``` yaml
host_configs:
- host_config_id: 1
  drive:
  - path: /dev/kikimr_ssd_00
    type: SSD
```

Файлы с примерами конфигурации, поставляемые в составе YDB Kubernetes operator, уже содержат такую секцию, и её не нужно менять.

## hosts — статические узлы кластера {#hosts}

В данной группе перечисляются статические узлы кластера, на которых запускаются процессы работы со Storage, и задаются их основные характеристики:

- Числовой идентификатор узла
- DNS-имя хоста и порт, по которым может быть установлено соединение с узлом в IP network
- Идентификатор [типовой конфигурации хоста](#host-configs)
- Размещение в определенной зоне доступности, стойке
- Инвентарный номер сервера (опционально)

**Синтаксис**

``` yaml
hosts:
- host: <DNS-имя хоста>
  host_config_id: <числовой идентификатор типовой конфигурации хоста>
  port: <порт> # 19001 по умолчанию
  location:
    unit: <строка с инвентарным номером сервера>
    data_center: <строка с идентификатором зоны доступности>
    rack: <строка с идентификатором стойки>
- host: <DNS-имя хоста>
  # ...
```

**Примеры**

``` yaml
hosts:
- host: hostname1
  host_config_id: 1
  node_id: 1
  port: 19001
  location:
    unit: '1'
    data_center: '1'
    rack: '1'
- host: hostname2
  host_config_id: 1
  node_id: 2
  port: 19001
  location:
    unit: '1'
    data_center: '1'
    rack: '1'
```

### Особенности Kubernetes {#hosts-k8s}

При развертывании YDB с помощью оператора Kubernetes секция `hosts` полностью генерируется автоматически, заменяя любой указанный пользователем контент в передаваемой оператору конфигурации. Все Storage узлы используют `host_config_id` = `1`, для которого должна быть задана [корректная конфигурация](#host-configs-k8s).

## domains_config — домен кластера {#domains-config}

Данный раздел содержит конфигурацию корневого домена кластера YDB, включая [конфигурации Blob Storage](#domains-blob) (хранилища бинарных объектов), [State Storage](#domains-state) (хранилища состояний) и [аутентификации](#auth).

**Синтаксис**

``` yaml
domains_config:
  domain:
  - name: <имя корневого домена>
    storage_pool_types: <конфигурация Blob Storage>
  state_storage: <конфигурация State Storage>
  security_config: <конфигурация аутентификации>
```

### Конфигурация Blob Storage {#domains-blob}

Данный раздел определяет один или более типов пулов хранения, доступных в кластере для данных в базах данных, со следующими возможностями конфигурации:

- Имя пула хранения
- Свойства устройств (например, тип дисков)
- Шифрование данных (вкл/выкл)
- Режим отказоустойчивости

Доступны следующие [режимы отказоустойчивости](../../cluster/topology.md):

Режим | Описание
--- | ---
`none` | Избыточность отсутствует. Применяется для тестирования.
`block-4-2` | Избыточность с коэффициентом 1,5, применяется для однодатацентровых кластеров.
`mirror-3-dc` | Избыточность с коэффициентом 3, применяется для мультидатацентровых кластеров.
`mirror-3dc-3-nodes` | Избыточность с коэффициентом 3. Применяется для тестирования.

**Синтаксис**

``` yaml
  storage_pool_types:
  - kind: <имя пула хранения>
    pool_config:
      box_id: 1
      encryption: <опциональный, укажите 1 для шифрования данных на диске>
      erasure_species: <имя режима отказоустойчивости - none, block-4-2, or mirror-3-dc>
      kind: <имя пула хранения - укажите то же значение, что выше>
      pdisk_filter:
      - property:
        - type: <тип устройства для сопоставления с указанным в host_configs.drive.type>
      vdisk_kind: Default
  - kind: <имя пула хранения>
  # ...
```

Каждой базе данных в кластере назначается как минимум один из доступных пулов хранения, выбираемый в операции создания базы данных. Имена пулов хранения среди назначенных могут быть использованы в атрибуте `DATA` при определении групп колонок в операторах YQL [`CREATE TABLE`](../../yql/reference/syntax/create_table.md#column-family)/[`ALTER TABLE`](../../yql/reference/syntax/alter_table.md#column-family).

### Конфигурация State Storage {#domains-state}

State Storage (хранилище состояний) -- это независимое хранилище в памяти для изменяемых данных, поддерживающее внутренние процессы YDB. Оно хранит реплики данных на множестве назначенных узлов.

Обычно State Storage не требует масштабирования в целях повышения производительности, поэтому количество узлов в нём следует делать как можно меньшим с учетом необходимого уровня отказоустойчивости.

Доступность State Storage является ключевой для кластера YDB, так как влияет на все базы данных, независимо от того какие пулы хранения в них используются.Для обеспечения отказоустойчивости State Storate его узлы должны быть выбраны таким образом, чтобы гарантировать рабочее большинство в случае ожидаемых отказов.

Для выбора узлов State Storage можно воспользоваться следующими рекомендациями:

|Тип кластера|Мин количество<br>узлов|Рекомендации по выбору|
|---------|-----------------------|-----------------|
|Без отказоустойчивости|1|Выберите один произвольный узел.|
|В пределах одной зоны доступности|5|Выберите пять узлов в разных доменах отказа пула хранения block-4-2, для гарантии, что большинство в 3 работающих узла (из 5) остается при сбое двух доменов.|
|Геораспределенный|9|Выберите три узла в разных доменах отказа внутри каждой из трех зон доступности пула хранения mirror-3-dc для гарантии, что большинство в 5 работающих узлов (из 9) остается при сбое зоны доступности + домена отказа.|

При развертывании State Storage на кластерах, в которых применяется несколько пулов хранения с возможным сочетанием режимов отказоустойчивости, рассмотрите возможность увеличения количества узлов с распространением их на разные пулы хранения, так как недоступность State Storage приводит к недоступности всего кластера.

**Синтаксис**
``` yaml
state_storage:
- ring:
    node: <массив узлов StateStorage>
    nto_select: <количество реплик данных в StateStorage>
  ssid: 1
```

Каждый клиент State Storage (например, таблетка DataShard) использует `nto_select` узлов для записи копий его данных в State Storage. Если State Storage состоит из большего количества узлов чем `nto_select`, то разные узлы могут быть использованы для разных клиентов, поэтому необходимо обеспечить, чтобы любое подмножество из `nto_select` узлов в пределах State Storage отвечало критериям отказоустойчивости.

Для `nto_select` должны использоваться нечетные числа, так как использование четных чисел не улучшает отказоустойчивость по сравнению с ближайшим меньшим нечетным числом.

### Конфигурация аутентификации {#auth}

[Режим аутентификации](../../concepts/auth.md) на кластере {{ ydb-short-name }} задается в разделе `domains_config.security_config`.

**Синтаксис**

``` yaml
domains_config:
  ...
  security_config:
    enforce_user_token_requirement: Bool
  ...
```

Ключ | Описание
--- | ---
`enforce_user_token_requirement` | Требовать токен пользователя.</br>Допустимые значения:</br><ul><li>`false` — режим аутентификации анонимный, токен не требуется (применяется по умолчанию, если параметр не задан);</li><li>`true` — режим аутентификации по логину и паролю, для выполнения запроса требуется валидный токен пользователя.</li></ul>

### Примеры {#domains-examples}

{% list tabs %}

- `block-4-2`

  ``` yaml
  domains_config:
    domain:
    - name: Root
      storage_pool_types:
      - kind: ssd
        pool_config:
          box_id: 1
          erasure_species: block-4-2
          kind: ssd
          pdisk_filter:
          - property:
            - type: SSD
          vdisk_kind: Default
    state_storage:
    - ring:
        node: [1, 2, 3, 4, 5, 6, 7, 8]
        nto_select: 5
      ssid: 1

- `block-4-2` + Auth

  ``` yaml
  domains_config:
    domain:
    - name: Root
      storage_pool_types:
      - kind: ssd
        pool_config:
          box_id: 1
          erasure_species: block-4-2
          kind: ssd
          pdisk_filter:
          - property:
            - type: SSD
          vdisk_kind: Default
    state_storage:
    - ring:
        node: [1, 2, 3, 4, 5, 6, 7, 8]
        nto_select: 5
      ssid: 1
    security_config:
      enforce_user_token_requirement: true

- `mirror-3-dc`

  ``` yaml
  domains_config:
    domain:
    - name: global
      storage_pool_types:
      - kind: ssd
        pool_config:
          box_id: 1
          erasure_species: mirror-3-dc
          kind: ssd
          pdisk_filter:
          - property:
            - type: SSD
          vdisk_kind: Default
    state_storage:
    - ring:
        node: [1, 2, 3, 4, 5, 6, 7, 8, 9]
        nto_select: 9
      ssid: 1
  ```

- `none` (без отказоустойчивости)

  ``` yaml
  domains_config:
    domain:
    - name: Root
      storage_pool_types:
      - kind: ssd
        pool_config:
          box_id: 1
          erasure_species: none
          kind: ssd
          pdisk_filter:
          - property:
            - type: SSD
          vdisk_kind: Default
    state_storage:
    - ring:
        node:
        - 1
        nto_select: 1
      ssid: 1
  ```

- Несколько пулов

  ``` yaml
  domains_config:
    domain:
    - name: Root
      storage_pool_types:
      - kind: ssd
        pool_config:
          box_id: '1'
          erasure_species: block-4-2
          kind: ssd
          pdisk_filter:
          - property:
            - {type: SSD}
          vdisk_kind: Default
      - kind: rot
        pool_config:
          box_id: '1'
          erasure_species: block-4-2
          kind: rot
          pdisk_filter:
          - property:
            - {type: ROT}
          vdisk_kind: Default
      - kind: rotencrypted
        pool_config:
          box_id: '1'
          encryption_mode: 1
          erasure_species: block-4-2
          kind: rotencrypted
          pdisk_filter:
          - property:
            - {type: ROT}
          vdisk_kind: Default
      - kind: ssdencrypted
        pool_config:
          box_id: '1'
          encryption_mode: 1
          erasure_species: block-4-2
          kind: ssdencrypted
          pdisk_filter:
          - property:
            - {type: SSD}
          vdisk_kind: Default
    state_storage:
    - ring:
        node: [1, 16, 31, 46, 61, 76, 91, 106]
        nto_select: 5
      ssid: 1
  ```

{% endlist %}

## Акторная система {#actor-system}

Основной потребитель CPU — акторная система. Все акторы, в зависимости от своего типа, выполняются в одном из пулов (параметр `name`). Конфигурирование заключается в распределении ядер процессора узла по пулам акторной системы. При выделении процессорных ядер пулам нужно учитывать, что PDisk и gRPC API работают вне акторной системы и требуют отдельных ресурсов.

Вы можете использовать [автоматическое](#autoconfig) или [ручное](#tuneconfig) конфигурирование акторной системы. В секции `actor_system_config` укажите:

* тип узла и количество ядер CPU, выделяемых процессу ydbd в случае использования автоматической конфигурирования;
* количество ядер CPU для каждой подсистемы кластера {{ ydb-full-name }} при использовании ручного конфигурирования.

Автоматическое конфигурирование является адаптивным, т.е. подстраивается под текущую нагрузку системы. Его рекомендуется использовать в большинстве случаев.

Ручное конфигурирование может быть полезно в случае, когда какой-либо пул акторной системы в процессе работы оказывается перегружен и это влияет на общую производительность БД. Отслеживать нагрузку пулов можно на [странице мониторинга в Embedded UI](../../maintenance/embedded_monitoring/ydb_monitoring.md#node_list_page).

### Автоматическое конфигурирование {#autoconfig}

Пример секции `actor_system_config` для автоматического конфигурирования акторной системы:

```yaml
actor_system_config:
  use_auto_config: true
  node_type: STORAGE
  cpu_count: 10
```

Параметр | Описание
--- | ---
`use_auto_config` | Включение автоматического конфигурирования акторной системы.
`node_type` | Тип узла. Определяет ожидаемую нагрузку и соотношение ядер CPU между пулами. Одно из значений:<ul><li>`STORAGE` — узел работает с блочными устройствами и отвечает за Distributed Storage;</li><li>`COMPUTE` — узел обслуживает пользовательскую нагрузку;</li><li>`HYBRID` — узел работает со смешанной нагрузкой или потребление `System`, `User` и  `IC` узла под нагрузкой приблизительно одинаково.
`cpu_count` | Количество ядер CPU, выделенных узлу.

### Ручное конфигурирование {#tuneconfig}

Пример секции `actor_system_config` для ручного конфигурирование акторной системы:

```yaml
actor_system_config:
  executor:
  - name: System
    spin_threshold: 0
    threads: 2
    type: BASIC
  - name: User
    spin_threshold: 0
    threads: 3
    type: BASIC
  - name: Batch
    spin_threshold: 0
    threads: 2
    type: BASIC
  - name: IO
    threads: 1
    time_per_mailbox_micro_secs: 100
    type: IO
  - name: IC
    spin_threshold: 10
    threads: 1
    time_per_mailbox_micro_secs: 100
    type: BASIC
  scheduler:
    progress_threshold: 10000
    resolution: 256
    spin_threshold: 0
```

Параметр | Описание
--- | ---
`executor` | Конфигурация пулов.<br>В конфигах пулов рекомендуется менять только количество ядер процессора (параметр `threads`).
`name` | Имя пула, определяет его назначение. Одно из значений:<ul><li>`System` — предназначен для выполнения быстрых внутренних операций {{ ydb-full-name }} (обслуживает системные таблетки, State Storage, ввод и вывод Distributed Storage, Erasure Сoding);</li><li>`User` — обслуживает пользовательскую нагрузку (пользовательские таблетки, выполнение запросов в Query Processor);</li><li>`Batch` — обслуживает задачи, которые не имеют строгого лимита на время выполнения, фоновых операции (сборка мусора, тяжелые запросы Query Processor);</li><li>`IO` — отвечает за выполнение всех задач с блокирующими операциями (аутентификация, запись логов в файл);</li><li>`IC` — Interconnect, включает нагрузку, связанную с коммуникацией между узлами (системные вызовы для ожидания и отправки по сети данных, сериализация данных, разрезание и склеивание сообщений).</li></ul>
`spin_threshold` | Количество тактов процессора перед уходом в сон при отсутствии сообщений. Состояние сна снижает энергопотребление, но может увеличивать latency запросов во время слабой нагрузки.
`threads` | Количество ядер процессора, выделенных пулу.<br>Не рекомендуется суммарно назначать в пулы System, User, Batch, IC больше ядер, чем доступно в системе.
`max_threads` | Максимальное количество ядер процессора, которые могут быть выданы пулу в случае использования простаивающих ядер из других пулов. При выставлении параметра включается механизм увеличения размера пула при полном потреблении пула и наличия свободных ядер.<br>Проверка текущей нагрузки и перераспределение ядер происходит 1 раз в секунду.
`max_avg_ping_deviation` | Дополнительное условие для расширения пула по количеству ядер. При потреблении более чем 90% ядер процессора, выделенных пулу, требуется ухудшение показателя SelfPing более чем на `max_avg_ping_deviation` микросекунд от ожидаемых 10 миллисекунд.
`time_per_mailbox_micro_secs` | Количество сообщений в каждом акторе, которое будет обработано перед переключением на другой актор.
`type` | Тип пула. Одно из значений:<ul><li>`IO` — укажите для пула IO;</li><li>`BASIC` — укажите для всех остальных пулов.</li></ul>
`scheduler` | Конфигурация шедулера. Шедулер акторной системы отвечает за доставку отложенных сообщений между акторами.<br>Не рекомендуется изменять параметры шедулера по умолчанию.
`progress_threshold` | В акторной системе есть возможность запросить отправку сообщения в будущем по расписанию. Возможна ситуация, когда в определенный момент времени системе не удастся отправить все запланированные сообщения. В этом случае система начинает рассылать сообщения в «виртуальном времени», обрабатывая в каждом цикле отправку сообщений за период, не превышающий `progress_threshold` в микросекундах, и продвигая виртуальное время на `progress_threshold`, пока оно не догонит реальное.
`resolution` | При составлении расписания отправки сообщений используются дискретные временные слоты. Длительность слота задается параметром `resolution` в микросекундах.

## blob_storage_config — статическая группа кластера {#blob-storage-config}

Укажите конфигурацию статической группы кластера. Статическая группа необходима для работы базовых таблеток кластера, в том числе `Hive`, `SchemeShard`, `BlobstorageContoller`.
Обычно данные таблетки не хранят много информации, поэтому мы не рекомендуем создавать более одной статической группы.

Для статической группы необходимо указать информацию о дисках и нодах, на которых будет размещена статическая группа. Например, для модели `erasure: none` конфигурация может быть такой:

```bash
blob_storage_config:
  service_set:
    groups:
    - erasure_species: none
      rings:
      - fail_domains:
        - vdisk_locations:
          - node_id: 1
            path: /dev/disk/by-partlabel/ydb_disk_ssd_02
            pdisk_category: SSD
# ...
```

Для конфигурации расположенной в 3 AZ  необходимо указать 3 кольца. Для конфигураций, расположенных в одной AZ, указывается ровно одно кольцо.

## Конфигурирование провайдеров аутентификации {#auth-config}

{{ ydb-short-name }} позволяет использовать различные способы аутентификации пользователя. Конфигурирование провайдеров аутентификации задается в секции `auth_config`.

### Конфигурация LDAP аутентификации {#ldap-auth-config}

Одним из способов аутентификации пользователей в {{ ydb-short-name }} является использование LDAP каталога. Подробнее о таком виде аутентификации написано в разделе про [взаимодействие {{ ydb-short-name }} с LDAP каталогом](../../concepts/auth.md#ldap-auth-provider). Для конфигурирования LDAP аутентификации необходимо описать секцию `ldap_authentication`.

Пример секции `ldap_authentication`:
```yaml
auth_config:
  #...
  ldap_authentication:
    host: "ldap-hostname.example.net"
    port: 389
    base_dn: "dc=mycompany,dc=net"
    bind_dn: "cn=serviceAccaunt,dc=mycompany,dc=net"
    bind_password: "serviceAccauntPassword"
    search_filter: "uid=$username"
    use_tls:
      enable: true
      ca_cert_file: "/path/to/ca.pem"
      cert_require: DEMAND
  ldap_authentication_domain: "ldap"
  refresh_time: "1h"
  #...
```
Параметр | Описание
--- | ---
`host` | Имя хоста, на котором работает LDAP сервер
`port` | Порт для подключения к LDAP серверу
`base_dn` | Корень поддерева в LDAP каталоге, начиная с которого будет производиться поиск записи пользователя
`bind_dn` | Отличительное имя (Distinguished Name, DN) сервисного аккаунта, от имени которого выполняется поиск записи пользователя
`bind_password` | Пароль сервисного аккаунта, от имени которого выполняется поиск записи пользователя
`search_filter` | Фильтр для поиска записи пользователя в LDAP каталоге. В строке фильтра может встречаться последовательность символов *$username*, которая будет заменена на имя пользователя, запрошенное для аутентификации в базе данных
`use_tls` | Настройки для конфигурирования TLS соединения между {{ ydb-short-name }} и LDAP сервером
`enable` | Определяет будет ли попытка установить TLS соединение
`ca_cert_file` | Путь до файла сертификата удостоверяющего центра
`cert_require` | Уровень требований к сертификату LDAP сервера.<br>Возможные значения:<ul><li>`NEVER` - {{ ydb-short-name }} не запрашивает сертификат или проверку проходит любой сертификат.</li><li>`ALLOW` - {{ ydb-short-name }} требует, что бы LDAP сервер предоставил сертификат. Если предоставленному сертификату нельзя доверять, TLS сессия все равно установится.</li><li>`TRY` - {{ ydb-short-name }} требует, что бы LDAP сервер предоставил сертификат. Если предоставленному сертификату нельзя доверять, установление TLS соединения прекращается.</li><li>`DEMAND` и `HARD` - Эти требования эквивалентны параметру `TRY`. По умолчанию установлено значение `DEMAND`.</li></ul>
`ldap_authentication_domain` | Идентификатор, прикрепляемый к имени пользователя, позволяющий отличать пользователей из LDAP каталога от пользователей аутентифицируемых с помощью других провайдеров. Значение по умолчанию `ldap`
`refresh_time` | Определяет время, когда будет попытка обновить информацию о пользователе. Конкретное время обновления будет лежать в интервале от `refresh_time/2` до `refresh_time`

## Примеры конфигураций кластеров {#examples}

В [репозитории](https://github.com/ydb-platform/ydb/tree/main/ydb/deploy/yaml_config_examples/)  можно найти модельные примеры конфигураций кластеров для самостоятельного развертывания. Ознакомьтесь с ними перед развертыванием кластера.