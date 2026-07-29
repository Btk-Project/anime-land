# 分集资源、JS Provider 与媒体缓存设计

> 文档状态：Draft  
> 适用范围：分集资源聚合、JS 在线 Provider、NekoAV 网络播放、缓存与下载  
> 依赖设计：[架构](../arch.md)、[Library 领域](../library/library_design.md)、
> [数据库](../database/local_database_design.md)、[QML UI](../view/qml_ui_design.md)  
> ABI 与实现状态：[插件运行时与工作空间](plugin_runtime.md)

## 1. 核心模型

资源库以本地 Catalog 的 Subject/Episode 为唯一目录骨架。某一集可用的资源由本地持久资源
和在线临时资源共同组成：

```text
Subject
└─ Episode
   ├─ LocalAssets（持久化）
   │  ├─ Video
   │  ├─ Subtitle
   │  ├─ Audio
   │  └─ Danmaku
   └─ OnlinePlayables（仅缓存）
      ├─ Provider A → Video + Subtitles...
      └─ Provider B → Video + Subtitles...
```

这里有四条硬边界：

1. Subject、Episode 和本地资源是长期领域对象，可以进入 Catalog/Library 数据库；
2. Provider 搜索出来的在线 playable 不是 Library 记录，不写 `media_resources`、
   `source_items` 或 `episode_media_links`；
3. 在线结果只存在于有 TTL 的运行时 cache，过期、插件重载或应用重启后重新搜索；
4. 用户明确执行“下载”后，完整产物才从在线临时资源晋升为本地持久资源。

“不持久化”指在线索引和播放描述不是业务数据库的事实来源。视频字节可以根据用户设置放在
内存或磁盘缓存中，但它仍是可淘汰 cache，不建立 Library 身份，也不保证永久存在。

## 2. 总体结构

```text
SubjectDetailPage / EpisodeRow
        │ EpisodeId、Provider 选择、临时 PlayableHandle
        ▼
EpisodeResourcesViewModel
        ▼
EpisodeResourceService
        ├────────► CatalogStore
        │          读取番剧名、别名、章节名、集数和类型
        ├────────► LibraryStore
        │          读取持久化的本地视频、字幕等
        ├────────► EpisodeProviderRegistry
        │                 │
        │          JsEpisodeProviderFactory
        │                 │
        │              QJSEngine
        │          JSON Request Runner / Host.html
        └────────► OnlinePlayableCache（TTL、非持久）

Local SourceItemId ───────────────┐
                                  ├─► EpisodePlaybackService
Online PlayableHandle ────────────┘            │
                                               ▼
                                      CachingMediaSource
                                               │
                                               ▼
                                          NekoAV
```

建议新增目录：

```text
src/
├─ model/
│  ├─ episode_resource/        # 分集资源聚合、Provider 接口与 cache
│  └─ playback/                # 本地/在线资源统一播放入口
├─ media/
│  └─ mediaio/                 # HTTP Range、字节缓存、下载和 AVIO bridge
├─ adapters/
│  └─ episode_provider_js/     # QJSEngine 与受控网络/HTML API
├─ presentation/
│  └─ episode_resource/        # 详情页 Provider 与资源状态
└─ view/qml/
   ├─ EpisodeResourcePanel.qml
   └─ OnlinePlayableCard.qml
```

Model 只依赖 `EpisodeProvider` 抽象，不依赖 `QJSEngine`。JS 是一个可替换 adapter，测试可以
注册纯 C++ Fake Provider。

## 3. 分集资源快照

Presentation 消费的是一次聚合快照，不直接拼 Catalog、Library 和 Provider 结果：

```cpp
enum class EpisodeAssetKind {
    Video,
    Subtitle,
    Audio,
    Danmaku,
};

struct LocalEpisodeAsset {
    SourceItemId sourceItemId;
    EpisodeAssetKind kind = EpisodeAssetKind::Video;
    QString displayName;
    std::optional<QString> language;
};

struct OnlinePlayableView {
    OnlinePlayableHandle handle;
    QString providerKey;
    QString providerName;
    QString displayName;
    std::vector<EpisodeAssetKind> availableKinds;
    std::optional<QDateTime> expiresAt;
};

struct EpisodeResourceSnapshot {
    EpisodeId episodeId;
    std::vector<LocalEpisodeAsset> localAssets;
    std::vector<OnlinePlayableView> onlinePlayables;
    std::vector<ProviderSearchState> providerStates;
};
```

`OnlinePlayableHandle` 是进程内不透明身份，例如 cache generation 与随机 ID 的组合。QML
只能把 handle 原样交回 Model，不能读取 provider JSON、URL、Cookie 或请求头。handle 过期
后返回稳定的 `OnlinePlayableExpired`，由页面重新搜索。

当前 `SourceItem` 默认表达可播放视频，没有资源种类。要长期支持本地字幕等资产，需要给
`SourceItem` 增加稳定的 `EpisodeAssetKind`，既有记录迁移时默认 `Video`；
`EpisodeMediaLink` 仍负责把这些本地资产关联到 Episode，不需要为字幕再造一套目录关系。

## 4. EpisodeProvider 接口

概念接口保持简单：Provider 按番剧与章节搜索，返回一组可播放资源。生产接口必须是异步、
可取消并能表达部分失败：

```cpp
struct EpisodeQuery {
    SubjectId subjectId;
    EpisodeId episodeId;
    QString subjectName;
    std::vector<QString> subjectAliases;
    QString episodeName;
    int episodeType = 0;
    std::optional<double> episodeNumber;
};

struct RemoteAsset {
    EpisodeAssetKind kind = EpisodeAssetKind::Video;
    MediaStreamType streamType = MediaStreamType::Unknown;
    QString displayName;
    std::optional<QString> language;
    std::optional<QString> mimeType;
    QJsonObject data;
};

struct OnlinePlayable {
    QString stableKey;
    QString displayName;
    std::vector<RemoteAsset> assets;
    std::optional<QDateTime> expiresAt;
};

class EpisodeProvider {
public:
    virtual ~EpisodeProvider() = default;

    virtual auto key() const -> QString = 0;
    virtual auto name() const -> QString = 0;
    virtual auto icon() const -> QUrl = 0;

    virtual auto ping()
        -> ilias::Task<EpisodeProviderResult<ProviderHealth>> = 0;

    virtual auto search(EpisodeQuery query)
        -> ilias::Task<EpisodeProviderResult<std::vector<OnlinePlayable>>> = 0;
};
```

相较于 `search(string name, string epname)`，结构化 `EpisodeQuery` 仍保留这两个核心字段，
但额外提供别名、集数和章节类型，避免插件只能依赖脆弱的字符串拼接。Provider 可以忽略不
需要的字段。

`OnlinePlayable` 是一个播放组合，通常包含一个主视频和零到多个字幕；同一 Provider 可以
返回不同线路或清晰度的多个 playable。首期校验规则：

- 至少有一个 `Video`；
- 只允许一个主视频，其他资源作为附件；
- `stableKey` 在同一次 Provider 搜索结果中唯一，仅用于 cache 刷新匹配；
- `RemoteAsset.data` 必须是可 JSON round-trip 的普通对象；
- URL、Cookie、Authorization 等敏感值只能留在运行时 cache，不写数据库和日志。

Provider 必须保留源站解析出的流类型，首期至少识别：

```cpp
enum class MediaStreamType {
    Unknown,
    Progressive,
    Hls,
    Dash,
};
```

流类型不能仅靠 URL 后缀猜测；JS Provider 应在能确定时显式返回。樱花动漫首个插件将
m3u8 标记为 `Hls`，播放器据此保留后续缓存、下载和格式扩展的决策空间。

如果某个站点必须在点击播放时再次生成签名 URL，可以把 `RemoteAsset.data` 当作 JSON
continuation，并在 Provider 内部返回惰性 playable。接口可以在真正需要时扩展一个可选
`resolve(OnlinePlayable)`；首期也允许 `search()` 直接返回已经可打开且带 `expiresAt` 的
playable。无论采用哪一种，过期结果都必须重新调用 Provider，不能长期落库。

## 5. Provider Registry 与 Factory

一个 JS 插件包可以注册多个 `EpisodeProvider`：

- `pluginId` 标识安装包；
- `providerId` 标识包内 Provider；
- `providerKey = pluginId + "." + providerId`，作为 cache 与设置身份；
- 名称和图标可以修改，不能充当稳定身份；
- Registry 拒绝重复 key，并保证一个插件的注册全成或全败。

`JsEpisodeProviderFactory` 负责：

1. 校验 manifest、入口、资源路径和权限；
2. 创建 JS engine 与 Host API；
3. 执行插件入口并收集 Provider；
4. 将完全注册成功的 Provider 原子发布到 Registry；
5. 插件禁用、重载或应用关闭时，先取消调用，再清理 cache，最后销毁 engine。

manifest 示例：

```json
{
  "manifestVersion": 1,
  "id": "org.example.anime",
  "name": "Example Provider",
  "version": "1.0.0",
  "runtime": {"api": "episode-provider", "version": 1},
  "entry": "index.js",
  "config": {
    "schema": "config.schema.json",
    "defaults": "config.defaults.json"
  },
  "permissions": {
    "network": ["https://example.test"],
    "media": ["https://*.example-cdn.test"]
  }
}
```

标识使用受限 ASCII 小写字符，禁止路径分隔符。入口、图标和模块 import 只能解析到插件包
内部；网络跳转后的最终域名也必须重新经过权限校验。

### 5.1 插件配置

插件代码与用户配置必须分开。镜像 URL、优先级、超时和线路偏好是数据变化，不应要求用户
修改 JS；页面结构、请求签名或 m3u8 解码算法变化才发布新插件版本。

一个插件包可以附带配置 Schema 和默认值：

```text
plugins/episode-providers/org.anime-land.yhdmmm/
├─ manifest.json
├─ index.js
├─ config.schema.json
└─ config.defaults.json
```

应用把用户覆盖值保存在自己的配置目录，而不是写回插件安装目录：

```text
${APP_CONFIG_DIR}/providers/org.anime-land.yhdmmm.json
```

应用设置的 `plugin_settings.plugins_directory` 默认是 `${APP_DATA_DIR}/plugins`；每次正常启动
自动扫描其 `episode-providers/` 一级子目录。扫描只加载完整包，不递归寻找零散 JS；坏包被
隔离，内置 plugin id 默认优先，发现/注册阶段不执行 ping 或任何网络请求。

镜像经常变化时，有效配置可以按三层合并：插件随包默认值 → 插件作者发布的已签名远程镜像
清单 → 用户本地覆盖。远程清单只能包含 Schema 允许的数据，经过 HTTPS、大小限制、签名和
域名权限校验后进入短期 cache；获取失败时继续使用最后一次有效清单或随包默认值，绝不下载
并执行远程 JS。

樱花 Provider 的首期配置建议为：

```json
{
  "schemaVersion": 1,
  "mirrors": [
    { "id": "primary", "baseUrl": "https://yhdmmm.com", "enabled": true, "priority": 100 },
    { "id": "www", "baseUrl": "https://www.yhdmmm.com", "enabled": true, "priority": 90 }
  ],
  "requestTimeoutMs": 10000,
  "minimumRequestIntervalMs": 750,
  "maximumRequestsPerOperation": 6,
  "maxCandidates": 2,
  "preferredLines": []
}
```

配置语义：

- `mirrors` 是用户可编辑的有序候选；`priority` 相同时保持配置顺序；
- Host 根据优先级和近期健康状态选择镜像；后续设置页可提供手动固定镜像；
- 网络/TLS/超时和 5xx 可以触发镜像 failover；“没有搜索结果”不是镜像故障；
- 健康状态、延迟、失败次数和 cooldown 到期时间只放运行时 cache，不反写用户配置；
- 新增镜像域名仍需满足 manifest 权限；超出声明范围时必须向用户请求授权；
- 配置只允许数据，不允许嵌入 JS、选择器或可执行表达式；不同 DOM 模板应由插件代码中的
  parser profile 处理；
- 保存配置后增加 provider config generation，取消旧调用并清除该 Provider 的搜索 cache；
- Provider 每次调用获得不可变配置快照，避免一次链路中途从镜像 A 切换到镜像 B。
- 一次 search → detail → play 解析链固定使用同一镜像；发生可重试的传输错误时，丢弃该链
  的中间 ID/URL，从下一个镜像重新开始，不能混用两个镜像的页面状态；
- Provider 结果 cache key 包含 config generation 和镜像身份，切换镜像不会命中旧结果；
- 用户配置使用临时文件加原子替换保存；Schema 升级无法迁移时保留旧文件备份并回退默认值。

插件可提供配置 Schema 供设置页生成 URL 列表、布尔值、整数和枚举控件，但 Schema 只负责
描述与基础校验，最终仍由 Provider 检查 URL scheme、必填字段和业务范围。秘密字段若未来
需要，必须使用应用的加密 credential 存储，不写普通 JSON。

直接编辑安装目录中的 JS 仅作为显式“开发者模式”能力：修改后插件标记为 dirty、禁用签名
与自动更新假设，并产生新的 runtime generation。正式安装的插件文件应视为只读，以便升级、
回滚和错误报告能够准确对应版本。

## 6. JS 接口

当前 ABI 使用同步 JSON 状态机，而不是把原始 `QNetworkAccessManager`、`QNetworkReply` 或
Promise fetch 暴露给 JS：

```js
AnimeLand.registerEpisodeProvider({
  id: "example",
  name: "Example Source",

  begin(operation, input, context) {
    return {
      type: "request",
      request: {url: "/search", method: "GET"},
      state: {phase: "search"}
    };
  },

  resume(state, response, context) {
    return {type: "complete", value: []};
  }
});
```

合法 Step 是 `request`、`complete` 或 `fail`。每一步先经过 JSON round-trip、大小限制和
C++ 校验；Host 执行请求后才把受限响应交给 `resume()`。搜索可以返回只含 JSON
continuation 的惰性 playable，用户选择后再调用 `resolve` 操作取得短期视频 URL。这使网络
次数、域名、重定向、限速和取消全部留在 C++ 控制面。

HTML 能力由 libxml2 recover parser 实现：

```js
const rows = AnimeLand.html.queryAll(response.text,
  "//a[contains(@class, 'episode')]", {
    href: "string(@href)",
    title: "normalize-space(string(.))"
  });
```

`lxml` 是 Python 包，不能直接注册进 `QJSEngine`；libxml2/XPath 是当前 C++ 等价实现。
完整 Step、Context、权限、工作空间和限制见
[插件运行时与工作空间](plugin_runtime.md)。

### 6.1 线程与隔离

- 每个插件一个独立 `QJSEngine` 和 `QNetworkAccessManager`；
- 同一 engine 的操作串行执行，网络 await 和取消由 C++/Ilias 管理；
- 当前首期 engine 位于创建它的 Qt composition 线程；内置包以及配置目录中自动发现的用户包
  都会加载，但用户目录必须视为受信任代码来源；
- QJSEngine 不能可靠中断同步死循环或提供硬 heap 配额，开放未审核插件前必须迁移到低权限
  独立进程，单独移动到 QThread 不视为安全沙箱；
- 页面关闭、重新搜索或插件重载后，迟到结果不能进入新 cache。

## 7. 在线结果缓存

在线资源分三层缓存，三者不能混用：

### 7.1 Provider 结果缓存

保存 `OnlinePlayable` 和 JSON data，仅使用内存：

```text
providerKey
+ plugin version/generation
+ SubjectId / EpisodeId
+ normalized query metadata
```

规则：

- Provider 可返回 TTL；未返回时应用使用较短默认 TTL，例如 5 分钟；
- `expiresAt` 比 cache TTL 更早时以前者为准；
- 搜索失败不覆盖仍有效的旧成功结果；
- 插件重载、禁用或权限变化立即清除对应 cache；
- cache 命中仍返回新的页面 handle generation，避免旧页面复用；
- cache 不写业务数据库，不作为应用重启后的离线索引。

### 7.2 元数据 HTTP 缓存

Provider 的 HTML/JSON 请求可以使用独立的短期 HTTP cache，但不得保存 Cookie、Token 或
Authorization。其容量、TTL 和清理策略与视频字节缓存分开。

### 7.3 视频字节缓存

任何在线主视频送入 NekoAV 前，都先经过 `CachingMediaSource`：

```text
QNetworkAccessManager / HTTP Range
              ↓
MemoryBufferStore 或 DiskBufferStore
              ↓
同步 read / seek bridge
              ↓
FFmpeg AVIOContext
              ↓
NekoAV
```

Disk backend 可以跨进程残留字节，但只具有 cache 语义：可随时淘汰、清空或因远端内容变化
失效，不在 Library 中显示为本地资源。用户若要求稳定离线文件，必须使用“下载”。

该小节是目标链路，不是第一期现状。第一期樱花 Provider 解析出 m3u8 后，直接把 URL 交给
现有 NekoAV `PlayBin`，HLS manifest 和分片由 FFmpeg 自己读取；应用尚不能证明所有媒体
字节经过 `CachingMediaSource`。在自定义 stream API 落地前，界面不得宣称严格视频缓存、
离线复用或可控硬配额已经支持。

## 8. 播放链路

本地与在线资源使用不同身份进入同一个播放用例：

```cpp
using EpisodeResourceRef =
    std::variant<SourceItemId, OnlinePlayableHandle>;

class EpisodePlaybackService {
public:
    auto open(EpisodeId episode, EpisodeResourceRef resource)
        -> ilias::Task<PlaybackResult<void>>;
};
```

本地链路：

```text
SourceItemId
  → LibraryStore descriptor
  → local-file provider
  → PlaybackInput
  → PlaybackSession
```

在线链路：

```text
OnlinePlayableHandle
  → OnlinePlayableCache
  → 校验 Provider generation 与 expiresAt
  → 主视频 RemoteAsset + 字幕附件
  → CachingMediaSource
  → PlaybackInput
  → PlaybackSession
```

handle 已过期时不直接使用其中 URL；Model 重新执行当前 EpisodeQuery，并按 Provider 与
`stableKey` 尝试定位新结果。无法唯一定位时刷新 UI，让用户重新选择。

当前 NekoAV `PlayBin` 只接受 URL，内部 `UrlSource` 直接调用 `avformat_open_input()`；完整
缓存链路需要增加自定义 `StreamSource`/`AVIOContext`，或让 `PlayBin` 支持注入 Source。
FFmpeg AVIO callback 是同步接口，因此异步网络先把 Range 数据写入有界 buffer，callback
只从 buffer 执行同步 read/seek。

第一期不实现上述 stream 输入。对于樱花 Provider 返回的 `MediaStreamType::Hls`，播放适配
层校验为 HTTP(S) m3u8 后，沿用当前 `PlaybackPipeline::open(QUrl)`；`PlaybackInput`/stream
接口只保留扩展点。需要自定义 header 时，第一期只有 FFmpeg URL 打开能力实际支持的场景
才可播放，不能通过把凭据拼进日志或数据库规避。

在线播放附带字幕时，字幕资源先完整进入小对象 cache，再交给字幕解析器；字幕 URL、请求头
和正文同样不进入 QML。某条线路在打开前失败时可以提示其他结果，开始传输后不静默拼接或
切换来源。

## 9. 用户界面

详情页不创建独立的在线资源库。每个 Episode 行展示：

- 已持久化的本地视频、字幕等；
- 已启用 Provider 列表及最近一次健康状态；
- “搜索在线资源”按钮；
- Provider 筛选与默认选择；
- 每个 Provider 独立的 Loading、Empty、Error 和 Result；
- 在线结果明确标记“临时结果”，过期后提示重新搜索；
- 点击 playable 展开视频线路和可用字幕，再由用户选择播放。

页面级“一键搜索”应理解为：对当前选中的 Episode 并行调用用户勾选的 Provider。默认不要
一次性对整部番的全部 Episode 执行 `章节数 × Provider 数` 个请求；若后续增加批量预取，
必须有显式操作、并发限制和速率提示。

用户可以持久保存“默认启用哪些 Provider”和 Provider 优先级，但不能持久保存某次在线
搜索结果。主播放按钮可以使用仍有效的上次会话选择；cache 已失效时必须重新搜索。

## 10. 媒体缓存设置

```cpp
enum class MediaBufferBackend {
    Memory,
    Disk,
};

struct MediaBufferSettings {
    MediaBufferBackend backend = MediaBufferBackend::Disk;
    std::string path = "${APP_CACHE_DIR}/media";
    std::uint64_t capacityBytes = 4ULL * 1024 * 1024 * 1024;
    std::uint64_t readAheadBytes = 64ULL * 1024 * 1024;
};
```

- `capacityBytes` 是缓存硬上限；
- `readAheadBytes` 是当前播放目标缓冲量；
- Memory 在进程退出时丢失；
- Disk 可以复用已有字节，但仍是可淘汰 cache；
- 设置路径变化只影响新任务，活动播放持有旧 store lease；
- 达到硬配额且没有可淘汰空间时施加背压并返回明确错误，不无限增长；
- cache key 使用 Provider、Episode、variant 和远端内容身份，不能只使用短期 URL；
- 内容身份变化时创建新 entry，禁止把新旧字节拼接。

Progressive MP4/MKV 的完整磁盘 cache 可能碰巧可由外部播放器打开，但产品不把 cache
承诺为用户文件。HLS/DASH 分片更不等于一个通用本地视频文件。

## 11. 下载是显式持久化例外

下载复用 Provider 搜索、HTTP Range、校验和重试能力，但写入不可淘汰的下载目录：

```text
Queued → Searching → Downloading → Verifying → Finalizing → Completed
```

- 临时文件使用任务专用 `.part`，成功后原子改名；
- URL 过期时重新调用 Provider，并校验远端内容身份后再续传；
- progressive 文件保留原始字节；
- HLS/DASH 需要导出本地 manifest/分片或显式 remux；
- 下载完成后由本地文件 provider 注册为新的持久 `SourceItem`；
- 原来的 OnlinePlayable 仍不持久化；
- 删除下载与“从 Library 移除”是不同操作，删除磁盘文件需要单独确认。

## 12. 错误与日志

至少区分：

- Provider 缺失、禁用或插件加载失败；
- JS 异常、超时、取消和非法返回值；
- 网络权限、HTTP、HTML/XML 解析失败；
- 没有搜索结果；
- Playable handle/URL 过期；
- 缺少主视频或附件格式不支持；
- Range 不支持、内容身份变化、缓存空间不足；
- NekoAV 打开或解码失败。

多 Provider 搜索允许部分成功。日志只记录 provider key、阶段、耗时、HTTP 状态和字节计数；
不记录 Cookie、Authorization、完整 query URL、HTML 正文或 `RemoteAsset.data`。

## 13. 持久化边界

允许长期保存：

- Subject/Episode Catalog；
- 本地视频、字幕等 SourceItem 和 EpisodeMediaLink；
- 插件包、启用状态、权限、Provider 默认筛选与优先级；
- 用户明确创建的下载任务和下载文件。

禁止进入业务数据库：

- Provider 搜索结果；
- OnlinePlayable、handle、RemoteAsset.data；
- 在线视频/字幕 URL、Cookie 和请求头；
- Provider HTML/JSON 响应；
- 可淘汰的视频缓存块。

## 14. 分阶段落地

### 阶段 A：资源聚合与 Fake Provider

- 已定义 `EpisodeAssetKind`、`EpisodeQuery`、`OnlinePlayable` 和 Provider 错误；
- 已实现 Registry、内存 TTL cache 与 generation；
- 使用 Fake Provider 接通 Episode 详情页搜索和临时 handle；
- 不加载 JS、不播放网络视频。

### 阶段 B：JS Provider

- 已实现 manifest、QJSEngine factory 和 `registerEpisodeProvider()`；
- 已实现基于 `QNetworkAccessManager` 的受控 JSON request runner；
- 已包装 libxml2 HTML recover parser 与 XPath 查询；
- 已覆盖超时、取消、域名跳转、响应/请求数上限和串行调用；插件热重载尚未接 UI。

### 阶段 C：本地多资产

- 为 SourceItem 增加 `EpisodeAssetKind`；
- 实现既有视频记录默认值和真实数据库迁移；
- 支持本地字幕导入、关联与分集资源快照。

### 阶段 D：严格在线播放

- 第一期先将 Provider 的 HLS/m3u8 URL 接到现有 `PlayBin`，并保留 `MediaStreamType`；
- 定义但不实现未来的 `PlaybackInput`、`AsyncMediaSource` 扩展边界；
- 后续实现 HTTP Range、Memory/Disk cache、lease、seek 与硬配额；
- 为 NekoAV 增加 StreamSource/AVIO bridge；
- 接通在线视频与字幕附件。

### 阶段 E：下载

- 实现 progressive 文件下载、断点续传、校验和原子完成；
- 将完成产物注册为本地 SourceItem；
- 再评估 HLS/DASH remux。

## 15. 最低验收测试

- 未缓存的 Episode 会调用勾选的 Provider，cache 命中不重复请求；
- TTL、插件重载、权限变化和应用重启后在线结果失效；
- 在线结果不会出现在 Library 数据库九张现有关系中；
- 多 Provider 搜索可部分成功，取消后的迟到结果不能污染新页面；
- QML 只获得临时 handle，不获得 URL、header、Cookie 或 JSON data；
- Provider 返回的视频/字幕组合经过 Schema 校验；
- 过期 handle 会重新搜索或要求用户选择，不播放陈旧 URL；
- 本地 SourceItem 与在线 playable 能聚合到同一个 Episode 快照；
- 第一期 HLS 结果保留 `MediaStreamType::Hls` 并通过现有 URL API 打开；
- stream API 落地后，每个交给 NekoAV 的网络视频字节都来自 buffer store；
- stream API 落地后，Memory/Disk cache 不超过硬配额，活动 lease 不被淘汰；
- 下载完成后出现新的本地 SourceItem，清理 cache 不删除下载；
- 日志和数据库中不存在在线凭据与短期签名 URL。

## 16. 当前代码的集成点

1. `src/model/episode_resource/` 已实现领域类型、Registry 与内存 cache，
   `src/adapters/episode_provider_js/` 已实现 ABI v1 Host；
2. GUI composition root 已加载并注册内置 `org.anime-land.yhdmmm` 包，加载阶段不发网络请求；
3. `EpisodeResourceService` 已组合 Registry、TTL cache、搜索与惰性 resolve；
4. `SubjectDetailsViewModel` 保持 Catalog/Library 详情职责；独立的
   `EpisodeResourcesViewModel` 已在章节行接入 Provider 选择、分源搜索状态、建议结果和播放；
5. `SourceItem` 需要资源种类才能持久表达本地字幕等附件；
6. 本地 `playEpisode()` 仍选择第一条媒体关系；在线播放已通过明确的
   `OnlinePlayableHandle` resolve 后进入播放器，后续本地多资源 UI 再改为明确 `SourceItemId`；
7. 在线 Provider 结果不能走 `LibraryStore::upsertDiscoveredMedia()`；
8. `PlaybackCommand::OpenMedia` 当前只有 `QUrl`，第一期可直接接 resolved m3u8，严格缓存需要
   内部 `PlaybackInput`；
9. NekoAV 当前 `PlayBin` 固定使用 `UrlSource`，后续严格 cache 需要自定义 stream 输入；
10. 设置模型新增 Provider 启用/权限/筛选与媒体 buffer 设置，不能复用 Bangumi HTTP cache。
