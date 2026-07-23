# 基于 Qt、Ilias、nekoav 与 Bangumi 的跨平台视频应用开发计划

> 文档状态：Draft  
> 目标版本：v0.1 MVP  
> 技术栈：C++23 / Qt 6 / Ilias / nekoav / FFmpeg / QRhi / SQLite / Bangumi API  
> 目标平台：Windows、Linux；macOS 作为后续移植目标  
> 项目定位：以 Bangumi 为元数据与追番进度中心，使用 nekoav 作为媒体框架，支持本地视频和可扩展网络媒体源的跨平台视频应用。

---

## 1. 文档目的

本文档用于：

1. 明确应用、Ilias、nekoav 和 Qt 之间的职责边界。
2. 规划 v0.1 至后续版本的开发阶段。
3. 支持两名开发者并行工作，减少模块冲突和重复建设。
4. 为任务拆分、代码评审、版本验收和风险管理提供依据。
5. 作为项目初期的架构约定，后续重大变更应同步更新本文档或 ADR。

---

## 2. 并行开发标记说明

本文使用以下标记：

| 标记 | 含义 |
|---|---|
| **[并行-A]** | 适合开发者 A 独立推进 |
| **[并行-B]** | 适合开发者 B 独立推进 |
| **[可并行]** | 任意开发者均可领取，且与同期主任务冲突较低 |
| **[串行依赖]** | 必须等待前置接口或功能完成 |
| **[共同评审]** | 建议两名开发者共同确定接口、行为或验收标准 |
| **[集成点]** | 两条并行工作流需要合并验证的位置 |

默认角色建议：

- **开发者 A：媒体核心负责人**
  - nekoav
  - PlaybackSession
  - 自定义媒体 I/O
  - QRhi 视频渲染
  - 字幕与弹幕渲染基础
- **开发者 B：应用与数据负责人**
  - Qt 应用壳
  - Bangumi
  - SQLite
  - 媒体库
  - 设置、缓存和应用 UI

角色不是固定权限边界；其目的只是降低两人同时修改同一目录的概率。

---

## 3. 产品范围

### 3.1 v0.1 MVP 目标

v0.1 应形成以下完整闭环：

1. 启动桌面应用。
2. 搜索和浏览 Bangumi 动画条目。
3. 查看条目详情及章节列表。
4. 选择本地媒体文件。
5. 将本地文件关联到 Bangumi 条目和章节。
6. 使用 nekoav 播放视频。
7. 支持播放、暂停、停止、Seek、音量、全屏。
8. 显示时长、当前位置和基本错误信息。
9. 保存本地播放进度。
10. 从最近播放中继续观看。
11. 在 Windows 和 Linux 上构建、安装和运行。

### 3.2 v0.1 暂不包含

以下功能不作为 v0.1 发布阻塞项：

- 在线资源站抓取
- BT 下载和边下边播
- 多设备云同步
- 弹幕在线服务
- 完整 ASS 特效兼容
- HDR 完整色彩管理
- GPU 硬件帧零拷贝
- Jellyfin、Emby 或 Plex
- Android、iOS
- macOS 正式支持
- 插件市场
- 自动更新

### 3.3 后续候选版本

#### v0.2

- Bangumi 登录与收藏同步
- 章节观看状态同步
- 外挂字幕
- 内嵌字幕轨选择
- 文件名自动匹配章节
- HTTP Range 网络媒体源
- 网络缓存和断点续播
- 播放倍速
- 音轨选择

#### v0.3

- 弹幕
- YUV/NV12/P010 QRhi Shader
- 硬件解码与硬件帧导入
- 网络源插件
- 下载任务
- macOS 支持
- QML 或 Qt Quick 前端评估

---

## 4. 总体架构

```text
┌───────────────────────────────────────────────┐
│                Qt Presentation                │
│ 窗口、页面、控件、快捷键、设置、系统集成       │
└──────────────────────┬────────────────────────┘
                       │ Signal / Property / DTO
┌──────────────────────▼────────────────────────┐
│               Application Layer               │
│ PlaybackSession / LibraryService              │
│ BangumiService / ProgressService / SyncService│
└──────────────────────┬────────────────────────┘
                       │ Ilias Task / Channel
┌──────────────────────▼────────────────────────┐
│                Infrastructure                 │
│ Bangumi Client / SQLite / HTTP / File / Cache │
│ Credential Store / MediaSource Factory        │
└──────────────────────┬────────────────────────┘
                       │
┌──────────────────────▼────────────────────────┐
│                     nekoav                    │
│ Pipeline / Element / Pad / Event / Message    │
│ FFmpeg / Audio / Subtitle / Clock / Decoder   │
└──────────────────────┬────────────────────────┘
                       │ VideoRenderer
┌──────────────────────▼────────────────────────┐
│                 QRhi Renderer                 │
│ CPU RGBA → YUV Shader → Hardware Frame Import │
└───────────────────────────────────────────────┘
```

### 4.1 核心边界

- Qt 不负责解码、同步或媒体网络读取。
- Bangumi 模块不直接依赖 nekoav。
- UI 不直接持有 `nekoav::Element`、`Pad` 或 `Pipeline`。
- 只有 `PlaybackSession` 可以直接管理当前播放 Pipeline。
- 所有播放命令必须经过串行命令通道。
- SQLite 访问必须通过统一 Repository 或数据库 Actor。
- 自定义网络源通过 `AsyncMediaSource` 接入，不把业务网络逻辑写进 FFmpeg Element。
- QRhi Renderer 只消费标准化 `VideoFrame`，不感知 Bangumi 或媒体库。

---

## 5. 代码仓库和目录建议

可以采用单仓库，也可以保持 nekoav、Ilias 独立仓库并由应用仓库引用。

### 5.1 推荐仓库关系

```text
Ilias                 独立基础设施仓库
nekoav                独立媒体框架仓库
video-app             应用仓库
```

应用通过固定 commit、tag 或包版本依赖 Ilias 和 nekoav。开发期可以使用本地 override。

### 5.2 应用仓库目录

```text
video-app/
├─ xmake.lua
├─ README.md
├─ docs/
│  ├─ project-plan.md
│  ├─ architecture.md
│  ├─ testing.md
│  └─ adr/
├─ src/
│  ├─ main.cpp
│  ├─ runtime/
│  ├─ presentation/
│  ├─ playback/
│  ├─ bangumi/
│  ├─ library/
│  ├─ persistence/
│  ├─ mediaio/
│  ├─ subtitle/
│  ├─ danmaku/
│  └─ platform/
├─ resources/
│  ├─ shaders/
│  ├─ icons/
│  └─ fonts/
├─ tests/
│  ├─ unit/
│  ├─ integration/
│  ├─ media/
│  └─ fixtures/
└─ packaging/
   ├─ windows/
   ├─ linux/
   └─ macos/
```

---

## 6. 核心模块设计

## 6.1 AppRuntime

**职责：**

- 创建并安装 `ilias::QIoContext`
- 初始化日志
- 初始化线程池与全局资源
- 运行数据库迁移
- 构建服务依赖
- 管理应用关闭顺序

```cpp
class AppRuntime {
public:
    auto start() -> ilias::Task<void>;
    auto shutdown() -> ilias::Task<void>;
};
```

**规则：**

- 不允许在析构函数中等待异步清理。
- 退出流程必须显式调用 `shutdown()`。
- `shutdown()` 必须先停止 PlaybackSession，再关闭数据库和网络服务。

---

## 6.2 PlaybackSession

`PlaybackSession` 是应用媒体控制面的唯一入口。

```cpp
class PlaybackSession {
public:
    auto run() -> ilias::Task<void>;
    auto close() -> ilias::Task<void>;
    auto send(PlaybackCommand command) -> ilias::IoTask<void>;

    auto snapshot() const -> PlaybackSnapshot;
};
```

### 播放命令

```cpp
using PlaybackCommand = std::variant<
    OpenMedia,
    Play,
    Pause,
    Stop,
    Seek,
    SetVolume,
    SetRate,
    SelectAudioTrack,
    SelectSubtitleTrack
>;
```

### 应用状态

```cpp
enum class PlaybackState {
    Idle,
    Opening,
    Ready,
    Playing,
    Paused,
    Seeking,
    Buffering,
    Stopping,
    Ended,
    Error
};
```

### 内部协程

```text
PlaybackSession::run
├─ commandLoop
├─ pipelineMessageLoop
├─ progressPersistenceLoop
└─ rendererLifecycleLoop
```

### 关键约束

- 命令通过有界 Channel 串行消费。
- 连续 Seek 应合并，仅处理最新目标。
- Open 新媒体前必须完整关闭旧 Pipeline。
- 所有关闭路径最终将 Pipeline 切换到 `State::Null`。
- UI 回调中禁止调用 `.wait()`。
- 旧 Session 的延迟消息必须通过 generation/session ID 丢弃。

---

## 6.3 nekoav 高层能力补全

v0.1 需要 nekoav 提供或稳定以下能力：

| 能力 | 优先级 |
|---|---:|
| Pipeline 完整异步状态切换 | P0 |
| 安全停止和 Teardown | P0 |
| Seek、Flush 和 EOS | P0 |
| 媒体加载信息 | P0 |
| 当前时钟消息 | P0 |
| 错误消息与来源 Element | P0 |
| Renderer 生命周期 | P0 |
| 音量控制 | P1 |
| 音轨枚举和选择 | P1 |
| 字幕轨枚举和选择 | P1 |
| 倍速 | P1 |
| Buffering 消息 | P1 |
| VideoFormatChanged 消息 | P1 |
| 自定义 AsyncMediaSource | P1 |
| 硬件帧输出 | P2 |

建议新增或稳定的 Message：

```cpp
using MessageStorage = std::variant<
    Error,
    StateChanged,
    ClockUpdate,
    MediaLoaded,
    Buffering,
    TracksChanged,
    ActiveTrackChanged,
    VideoFormatChanged,
    SeekBegin,
    SeekEnd,
    EndOfStream
>;
```

---

## 6.4 QRhi 视频渲染

### v0.1 路径

```text
FFmpeg VideoFrame
    → RGBA Frame
    → latest-frame mailbox
    → Qt queued invoke
    → QRhi texture upload
    → draw
```

### 必须明确的行为

- Renderer mailbox 容量为 1。
- 新帧覆盖尚未显示的旧帧。
- UI 线程只取最新帧。
- Renderer shutdown 后禁止继续提交。
- Widget 销毁时断开 Proxy。
- RHI 设备变化时重建全部 GPU 资源。
- RenderPassDescriptor 变化时重建 Pipeline。
- 支持 KeepAspectRatio。
- 正确处理 stride、旋转、SAR/DAR 和 HiDPI。

### 后续优化

1. YUV420P 多纹理。
2. NV12 双平面。
3. P010 和 10-bit。
4. Shader 色彩空间转换。
5. D3D11、VAAPI、VideoToolbox、Vulkan 硬件帧导入。

---

## 6.5 AsyncMediaSource

自定义媒体 I/O 接口：

```cpp
class AsyncMediaSource {
public:
    virtual ~AsyncMediaSource() = default;

    virtual auto open()
        -> ilias::IoTask<MediaSourceInfo> = 0;

    virtual auto readAt(
        std::uint64_t offset,
        std::span<std::byte> buffer
    ) -> ilias::IoTask<std::size_t> = 0;

    virtual auto size() const
        -> std::optional<std::uint64_t> = 0;

    virtual auto close()
        -> ilias::IoTask<void> = 0;
};
```

实现计划：

```text
LocalFileSource       v0.1
HttpRangeSource       v0.2
CachedSource          v0.2
MemorySource          测试辅助
TorrentPieceSource    后续
```

FFmpeg AVIO callback 不直接等待异步网络请求，使用同步桥接：

```text
Async producer
    → bounded range cache / ring buffer
    → synchronous AVIO callback
```

Seek 必须：

1. 增加 generation。
2. 取消旧请求。
3. 清理旧缓存。
4. 设置目标 offset。
5. 发起新一轮预读。
6. 丢弃旧 generation 返回的数据。

---

## 6.6 Bangumi 模块

### 接口

```cpp
class BangumiRepository {
public:
    auto searchSubjects(QString query)
        -> Task<Result<std::vector<Subject>>>;

    auto getSubject(std::int64_t id)
        -> Task<Result<SubjectDetails>>;

    auto getEpisodes(std::int64_t subjectId)
        -> Task<Result<std::vector<Episode>>>;

    auto getCollections()
        -> Task<Result<std::vector<UserCollection>>>;

    auto updateEpisodeProgress(
        std::int64_t episodeId,
        EpisodeProgress progress
    ) -> Task<Result<void>>;
};
```

### 设计要求

- DTO 与领域模型分离。
- 匿名 API 与用户认证 API 分离。
- 每个请求支持取消和超时。
- 请求设置合法 User-Agent。
- 不在 UI 中解析 JSON。
- 图片缓存和 JSON 缓存独立。
- 网络失败时优先返回本地缓存。
- Token 不写入日志。

---

## 6.7 媒体库与章节关联

### 领域对象

```cpp
struct MediaResource {
    SourceResourceId id;
    QString providerKey;
    QString stableKey;
    int descriptorVersion;
};

struct PlaybackProgress {
    EpisodeId episodeId;
    std::optional<SourceItemId> lastSourceItemId;
    std::chrono::milliseconds position;
    std::optional<std::chrono::milliseconds> duration;
    bool completed;
    QDateTime updatedAt;
};
```

本地 ID、外部身份、媒体资源根和可播放项的完整边界以
[`database/local_database_design.md`](database/local_database_design.md) 为准。
UI 和播放层不得直接把 Bangumi ID、文件路径或 provider 私有 descriptor 当作核心
对象身份。

### 文件匹配等级

| 匹配方式 | 可信度 |
|---|---:|
| 用户手动指定 | 最高 |
| 文件名解析 | 中等 |
| 文件顺序推断 | 低，必须确认 |

第一版支持：

```text
[字幕组] 作品名 - 01 [1080p].mkv
作品名 S01E03.mp4
作品名 EP12.mp4
```

匹配器接口：

```cpp
class MediaMatcher {
public:
    auto match(
        const MediaFileInfo& file,
        std::span<const Episode> episodes
    ) -> std::vector<MediaMatchCandidate>;
};
```

---

## 6.8 SQLite 与持久化

### 表结构

v0.1 的表结构、Migration、Store API 和事务边界以
[`database/local_database_design.md`](database/local_database_design.md) 为实施契约。
本节不再维护一份容易漂移的表名副本。

`bangumi_cache`、`sync_queue`、账号同步和图片二进制缓存不属于 v0.1 核心数据库；
已经浏览过的条目通过标准化 `subjects`、`tags` 和 `episodes` 数据离线可读。

### 数据库规则

- 所有写入串行化。
- 播放位置不按每个 ClockUpdate 写入。
- 每 10 至 15 秒保存一次。
- Pause、Stop、切集和退出时立即保存。
- Schema 变更必须添加 migration。
- 测试使用临时数据库。
- Repository 不向 UI 暴露 SQL。
- SQLite 外键必须在开始 Migration 事务前启用并验证。
- 使用 ilias-sql 执行 Migration 时，每条 DDL 独立提交给驱动；不得假设一次
  `execute()` 会消费多条 SQL。

---

## 6.9 字幕与弹幕

### 字幕管线

```text
Subtitle Packet
    → Subtitle Decoder
    → Subtitle Event Model
    → Layout
    → QRhi Overlay
```

### 弹幕管线

```text
Danmaku Source
    → Normalize
    → Timeline
    → Collision Scheduler
    → QRhi Overlay
```

字幕和弹幕共享播放时钟，但必须使用独立模型和调度器。

v0.1 不要求完整实现，只要求在架构上保留 Overlay Renderer 接口。

---

## 7. 双人并行开发方案

## 7.1 工作流 A：媒体核心

**负责人建议：开发者 A**

主要目录：

```text
src/playback/
src/mediaio/
src/render/
nekoav 仓库
```

主要任务：

- PlaybackSession
- 命令 Channel
- Pipeline 消息适配
- nekoav 状态和错误补全
- QRhi Renderer
- LocalFileSource
- Seek、Stop、切媒体稳定性
- 媒体测试样本和压力测试

## 7.2 工作流 B：应用与数据

**负责人建议：开发者 B**

主要目录：

```text
src/presentation/
src/bangumi/
src/library/
src/persistence/
src/platform/
```

主要任务：

- 应用窗口和导航
- Bangumi Client
- 领域模型
- SQLite Schema 和 Repository
- 媒体库页面
- 条目详情和章节列表
- 文件关联 UI
- 设置和缓存
- 打包脚本基础

## 7.3 双方共享接口

以下文件或接口需共同维护：

```text
src/playback/playback_command.hpp
src/playback/playback_snapshot.hpp
src/library/media_resource.hpp
src/runtime/service_registry.hpp
docs/architecture.md
docs/adr/
```

共享接口修改规则：

1. 先提交 ADR 或接口提案。
2. 至少一名另一工作流开发者评审。
3. 合并后双方基于新接口继续工作。
4. 避免在未沟通情况下同时重构共享 DTO。

---

## 8. 分阶段实施计划

时间以两名开发者为参考。单人开发时可按依赖顺序执行。

# 阶段 0：架构基线与工程脚手架

建议周期：3 至 5 天

### [共同评审]

- 确认 v0.1 范围。
- 确认仓库结构。
- 确认 xmake 依赖方式。
- 确认日志、错误码和命名规范。
- 确认 PlaybackCommand 与 PlaybackSnapshot。
- 确认 DTO 不直接暴露 nekoav 类型。

### [并行-A]

- 创建 `PlaybackSession` 空实现。
- 建立 command channel 和 session generation。
- 添加 FakePipeline 或测试替身。
- 建立媒体测试 fixture 目录。

### [并行-B]

- 创建 Qt 应用壳。
- 创建页面导航和空页面。
- 建立 SQLite migration 框架。
- 定义 Subject、Episode、MediaResource 等领域对象。

### [集成点]

- UI 能订阅 Fake PlaybackSnapshot。
- 点击播放按钮能发送 Fake PlaybackCommand。
- 数据库能启动、迁移和关闭。

### 验收

- Windows、Linux CI 能构建。
- 应用能正常启动和退出。
- 没有 UI 线程 `.wait()`。
- 基础测试框架可运行。

---

# 阶段 1：本地媒体播放闭环

建议周期：1 至 2 周

### [并行-A]

- 接入真实 nekoav Pipeline。
- 实现 Open、Play、Pause、Stop、Seek。
- 实现 Pipeline Message 消费。
- 实现 Renderer 初始化和 shutdown。
- 完成 RGBA QRhiWidget。
- 增加连续 Open/Stop/Seek 测试。
- 补全必要的 nekoav Error 和 State 消息。

### [并行-B]

- 实现播放器页面。
- 实现控制栏、进度条、音量和全屏。
- 实现文件选择器。
- 实现最近播放页面原型。
- 实现播放进度 Repository。
- 实现应用设置基础。

### [串行依赖]

- UI 进度条依赖 PlaybackSnapshot 的 position/duration。
- 错误展示依赖统一 PlaybackError。
- 播放状态图标依赖 PlaybackState。

### [集成点]

- 使用真实本地 MP4/MKV 完成播放。
- 关闭窗口时异步停止 Pipeline。
- 切换文件时旧 Pipeline 不再产生 UI 更新。
- Seek 时 UI 正确显示 Seeking 状态。

### 验收

- 连续播放 2 小时无崩溃。
- 连续 Seek 100 次无死锁。
- 连续切换文件 50 次无明显泄漏。
- 窗口销毁后 Renderer 不再收帧。
- 播放位置可持久化并恢复。

---

# 阶段 2：Bangumi 和媒体库

建议周期：1 至 2 周

### [并行-A]

- 稳定 PlaybackSession 公共接口。
- 增加媒体元信息输出。
- 增加自动下一集所需 EOS 行为。
- 实现 LocalFileSource 正式接口。
- 补充媒体格式兼容性测试。

### [并行-B]

- 实现 Bangumi 搜索。
- 实现条目详情和章节列表。
- 持久化已浏览条目的标准化目录数据。
- 实现本地文件导入。
- 实现文件与章节手动关联。
- 实现最近播放和继续观看。
- 实现文件名自动匹配初版。

### [可并行]

- 应用图标和基本视觉设计。
- 键盘快捷键。
- 错误提示文案。
- 本地化框架。
- CI 缓存和构建产物上传。

### [集成点]

- 从 Bangumi 章节页面打开关联媒体。
- EOS 后记录章节完成状态。
- 下一集存在且已关联时可自动播放。
- 播放页显示条目名和章节名。

### 验收

- 可完成“搜索条目 → 关联文件 → 播放 → 保存进度”的完整路径。
- 无网络时可浏览已缓存条目。
- 数据库关联关系重启后保留。
- Bangumi 网络失败不影响本地媒体播放。

---

# 阶段 3：网络媒体 I/O

建议周期：2 周

### [并行-A]

- 定义 AsyncMediaSource。
- 实现 HttpRangeSource。
- 实现 AVIO Bridge。
- 实现有界缓存、预读和取消。
- 实现 Range Seek。
- 实现 Buffering Message。
- 添加本地 HTTP 测试服务器和故障注入。

### [并行-B]

- 实现网络源配置 UI。
- 支持自定义 Header、Cookie 和 Referer。
- 实现缓存目录设置。
- 实现网络速度、缓冲状态 UI。
- 实现网络错误和重试提示。
- 实现缓存清理页面。

### [共同评审]

- 确认 HTTP Source 不泄漏凭据到日志。
- 确认 Range 缓存策略。
- 确认取消和 generation 规则。
- 确认重试不会重复请求失控。

### [集成点]

- HTTP MP4/MKV 可播放和 Seek。
- 快速连续 Seek 只保留最新目标。
- 断网时进入 Buffering，恢复后继续。
- Stop 后所有网络请求立即取消。

### 验收

- 受限带宽下不会无限增长内存。
- 服务器不支持 Range 时行为明确。
- 401、403、404、超时和中断均有可读错误。
- 退出应用后无遗留网络任务。

---

# 阶段 4：字幕、音轨和弹幕基础

建议周期：2 周

### [并行-A]

- 补全音轨和字幕轨枚举。
- 实现轨道切换。
- 实现 Subtitle Decoder。
- 实现 Overlay Renderer 接口。
- 实现 QRhi 字幕基础渲染。
- 预留 Danmaku Renderer。

### [并行-B]

- 实现轨道选择 UI。
- 实现外挂字幕选择。
- 实现字幕延迟设置。
- 定义弹幕数据模型。
- 实现弹幕过滤和显示设置页面。
- 准备弹幕测试数据。

### [集成点]

- Seek 后字幕状态正确重建。
- 切字幕轨不会重建整个应用。
- 全屏、HiDPI 下字幕位置正确。
- 字幕和视频共享同一播放时钟。

### 验收

- SRT、ASS 基础字幕可显示。
- 外挂字幕可加载和切换。
- Subtitle Renderer shutdown 安全。
- 弹幕接口可用 Fake 数据驱动。

---

# 阶段 5：性能与跨平台强化

建议周期：持续进行

### [并行-A]

- YUV420P/NV12/P010 Shader。
- 色彩空间和 range 处理。
- 硬件解码策略。
- D3D11 硬件帧导入调研。
- VAAPI/Vulkan 路径调研。
- 帧耗时和丢帧指标。

### [并行-B]

- Windows 安装包。
- Linux AppImage、Flatpak 或发行版包评估。
- 自动更新方案调研。
- 崩溃报告与诊断包。
- macOS 构建和 Ilias backend 验证。
- 文档和用户引导。

### [共同评审]

- 是否将 UI 迁移至 QML。
- 是否启用硬件帧零拷贝。
- 是否增加插件 ABI。
- 是否支持移动端。

---

## 9. 推荐任务分配表

| 编号 | 任务 | 标记 | 负责人建议 | 前置 |
|---|---|---|---|---|
| P-001 | PlaybackCommand 定义 | [共同评审] | A+B | 无 |
| P-002 | PlaybackSnapshot 定义 | [共同评审] | A+B | 无 |
| P-003 | PlaybackSession commandLoop | [并行-A] | A | P-001 |
| P-004 | Pipeline Message Adapter | [并行-A] | A | P-002 |
| P-005 | Qt 应用壳 | [并行-B] | B | 无 |
| P-006 | 播放器控制页 | [并行-B] | B | P-001/P-002 |
| P-007 | QRhi RGBA Renderer | [并行-A] | A | 无 |
| P-008 | SQLite Migration | [并行-B] | B | 无 |
| P-009 | PlaybackProgress Repository | [并行-B] | B | P-008 |
| P-010 | nekoav Stop/Teardown 压测 | [并行-A] | A | P-003 |
| B-001 | Bangumi Client | [并行-B] | B | 无 |
| B-002 | Bangumi Cache | [并行-B] | B | B-001/P-008 |
| B-003 | 条目和章节页面 | [并行-B] | B | B-001 |
| L-001 | 媒体扫描 | [并行-B] | B | P-008 |
| L-002 | 文件章节关联 | [并行-B] | B | B-003/L-001 |
| L-003 | 文件名 Matcher | [可并行] | A 或 B | L-001 |
| N-001 | AsyncMediaSource 接口 | [共同评审] | A+B | 本地播放稳定 |
| N-002 | HttpRangeSource | [并行-A] | A | N-001 |
| N-003 | 网络源配置 UI | [并行-B] | B | N-001 |
| S-001 | Track Message | [并行-A] | A | Pipeline 稳定 |
| S-002 | 轨道选择 UI | [并行-B] | B | S-001 |
| T-001 | Windows 打包 | [可并行] | B | MVP 可运行 |
| T-002 | 媒体压力测试 | [并行-A] | A | MVP 可运行 |

---

## 10. 接口冻结节点

为了避免双人并行时频繁互相阻塞，设置以下接口冻结节点。

### Freeze 1：阶段 0 完成

冻结：

- `PlaybackCommand`
- `PlaybackState`
- `PlaybackSnapshot`
- `PlaybackError`
- `MediaResource`

允许新增字段，但破坏性修改需要共同评审。

### Freeze 2：阶段 1 完成

冻结：

- PlaybackSession 对 UI 的公共接口
- Pipeline Message Adapter
- Renderer 生命周期接口
- 播放进度 Repository 接口

### Freeze 3：阶段 2 完成

冻结：

- Subject 和 Episode 领域模型
- 媒体与章节关联模型
- 数据库 v1 Schema

### Freeze 4：阶段 3 完成

冻结：

- AsyncMediaSource
- MediaSourceFactory
- 缓存与 Range 读取语义

---

## 11. Git 协作建议

### 分支

```text
main                 稳定分支
dev                  集成分支，可选
feature/playback-*   媒体核心
feature/bangumi-*    Bangumi
feature/library-*    媒体库
feature/render-*     QRhi
feature/mediaio-*    自定义媒体 I/O
fix/*
```

如果团队只有两人，也可以直接以 `main + feature branches` 工作，避免长期 dev 分支漂移。

### Pull Request 规则

- 一个 PR 只处理一个模块或一个明确行为。
- 修改共享接口必须由另一名开发者评审。
- PR 描述需包含：
  - 修改目标
  - 接口变化
  - 测试方法
  - 已知限制
  - 截图或日志
- 禁止在功能 PR 中顺带大规模格式化无关文件。
- nekoav 与应用的关联修改应使用两个 PR，并在描述中互相链接。
- 应用依赖 nekoav 的 commit 应固定，验证后再升级。

### 提交建议

```text
feat(playback): add serialized command loop
feat(bangumi): add subject search endpoint
fix(render): ignore frames after renderer shutdown
test(mediaio): cover cancelled range request
docs(adr): define playback session ownership
```

---

## 12. 测试计划

## 12.1 单元测试

- PlaybackState 转换
- Command 合并
- Session generation
- 文件名 Matcher
- Bangumi DTO 映射
- 播放完成判定
- 数据库 Repository
- 缓存淘汰
- Range 计算

## 12.2 集成测试

- Pipeline Open/Play/Pause/Stop
- Seek + Flush
- EOS
- Renderer shutdown
- SQLite migration
- Bangumi 缓存回退
- HTTP Range
- 取消中的网络请求
- 切换媒体时旧消息丢弃

## 12.3 媒体测试集

建议保留可合法分发的小型测试样本：

```text
H.264 + AAC MP4
H.264 + AAC MKV
H.265 + AAC MKV
VP9 + Opus WebM
多音轨 MKV
内嵌 SRT
内嵌 ASS
可变帧率
非方形像素
旋转元数据
损坏文件
无音频文件
纯音频文件
```

## 12.4 压力测试

- 播放 4 小时。
- 连续 Seek 100 次。
- 连续切换媒体 50 次。
- 窗口反复全屏 100 次。
- 重建 QRhi 资源。
- 网络反复断开恢复。
- 关闭应用时仍在 Open、Seek 或 Buffering。
- 快速切换章节导致多个请求并发取消。

---

## 13. v0.1 Definition of Done

v0.1 必须满足：

### 功能

- Windows、Linux 能启动和退出。
- 能搜索 Bangumi 条目。
- 能显示条目详情和章节。
- 能关联本地文件和章节。
- 能使用 nekoav 播放媒体。
- 支持播放、暂停、Stop、Seek、音量、全屏。
- 能保存和恢复进度。
- 能显示最近播放。
- EOS 能正确结束或进入下一集策略。
- 无网络时本地播放仍正常。

### 稳定性

- UI 线程无 `.wait()`。
- 关闭时所有协程可取消。
- Pipeline 最终回到 `Null`。
- Renderer 销毁后不收帧。
- 旧 Session 消息不会污染新 Session。
- Bangumi 请求取消后不继续更新 UI。
- 数据库关闭前完成必要写入。

### 工程

- CI 覆盖 Windows、Linux。
- 核心模块有测试。
- 不记录 Token、Cookie 等敏感信息。
- 第三方许可证随安装包分发。
- README 包含构建和运行步骤。
- 已知问题记录在发布说明中。

---

## 14. 风险清单

| 风险 | 等级 | 缓解措施 |
|---|---:|---|
| nekoav 接口在应用开发中频繁变化 | 高 | 设置接口冻结节点；应用只依赖 PlaybackSession |
| FFmpeg 阻塞任务占满 blocking pool | 高 | 压力测试；必要时独立媒体 Context 或线程池 |
| Seek、Stop、Open 并发竞态 | 高 | 单 commandLoop；generation；统一关闭路径 |
| QRhi 私有 API 兼容性 | 高 | 固定 Qt 版本；封装 Renderer Core；避免扩散私有头文件 |
| 网络 AVIO 桥接死锁 | 高 | 同步 callback 只读有界缓存；不在 callback 内等待 executor |
| SQLite 阻塞 Qt 主线程 | 中 | 数据库 Actor 或专用执行线程 |
| Bangumi API 变化或限流 | 中 | DTO 隔离；缓存；退避；合法 User-Agent |
| 跨平台硬件解码差异 | 中 | v0.1 使用软件或可回退路径；硬件解码后置 |
| 双人修改共享 DTO 冲突 | 中 | 共享接口评审；小 PR；接口冻结 |
| 范围膨胀 | 高 | v0.1 不加入 BT、弹幕在线服务、零拷贝和移动端 |

---

## 15. 建议的第一批 Issue

### Milestone：Foundation

- [ ] 建立应用仓库和 xmake 工程
- [ ] 定义 PlaybackCommand
- [ ] 定义 PlaybackSnapshot
- [ ] 实现 AppRuntime
- [ ] 建立 SQLite migration
- [ ] 建立 Windows/Linux CI
- [ ] 创建 FakePlaybackSession

### Milestone：Local Playback

- [ ] 实现真实 PlaybackSession
- [ ] 接入 nekoav Pipeline
- [ ] 移除示例中的所有 `.wait()`
- [ ] 实现 QRhi RGBA Renderer
- [ ] 实现 Stop/Teardown
- [ ] 实现进度持久化
- [ ] 实现播放器 UI
- [ ] 增加 Seek 压力测试

### Milestone：Bangumi Library

- [ ] 实现 Bangumi Client
- [ ] 实现搜索页
- [ ] 实现条目详情
- [ ] 实现章节列表
- [ ] 实现媒体扫描
- [ ] 实现文件关联
- [ ] 实现最近播放
- [ ] 实现自动文件名匹配初版

---

## 16. 推荐启动顺序

两人加入时，第一周可以这样安排：

### 开发者 A

1. PlaybackSession 骨架。
2. Command Channel。
3. 真实 nekoav Pipeline 接入。
4. 完整异步 Stop。
5. QRhi Renderer 从示例迁移为可复用组件。
6. Open/Stop/Seek 集成测试。

### 开发者 B

1. Qt 应用壳和页面导航。
2. 领域模型。
3. SQLite migration 和 Repository。
4. Bangumi Client。
5. FakePlaybackSession 驱动播放器 UI。
6. 条目搜索和详情原型。

### 第一周末集成目标

- 开发者 B 的 UI 从 FakePlaybackSession 切换到开发者 A 的真实 PlaybackSession。
- 能从播放器页面选择本地文件并播放。
- 播放状态和进度通过 Snapshot 更新 UI。
- 退出应用时无阻塞等待、崩溃或悬空任务。

---

## 17. 决策记录建议

以下事项建议建立 ADR：

```text
ADR-001：使用 Ilias 作为应用统一异步运行时
ADR-002：使用 PlaybackSession 隔离 UI 与 nekoav
ADR-003：播放命令采用单有界 Channel 串行执行
ADR-004：v0.1 使用 QRhiWidget 和 RGBA 上传
ADR-005：SQLite 访问采用单写入执行域
ADR-006：自定义媒体 I/O 使用 AsyncMediaSource + AVIO Bridge
ADR-007：v0.1 目标平台为 Windows 和 Linux
ADR-008：Bangumi DTO 与领域模型分离
```

---

## 18. 项目完成路径摘要

```text
工程脚手架
    ↓
PlaybackSession + QRhi 本地播放
    ↓
播放进度和最近播放
    ↓
Bangumi 搜索、详情、章节
    ↓
媒体文件与章节关联
    ↓
网络媒体 I/O
    ↓
字幕、音轨和弹幕
    ↓
YUV、硬件解码和跨平台优化
```

项目最重要的近期目标不是完成大量页面，而是先得到一个：

> 可反复 Open、Play、Pause、Seek、Stop，可安全取消和退出，并能稳定通过 QRhi 输出的全异步 PlaybackSession。

一旦这一基础稳定，Bangumi、媒体库、字幕、弹幕和网络源都可以作为相对独立的模块持续并行加入。
