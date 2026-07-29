# Bangumi 模块设计索引

> 文档状态：Living Design  
> 当前阶段：浏览器登录闭环 + 公开条目搜索/每日放送/详情/章节读取 + Collection Read 能力参考切片
> 前端架构：View–Presentation–Model；`anime-land-cli` 使用 MVP Presenter，`anime-land` 使用 Qt Quick/QML ViewModel

本文件只负责导航、当前状态和跨专题约束。协议细节、接口字段和测试清单放在专题文档中，避免每次修改都加载一份不断膨胀的总设计。

## 专题文档

| 想了解或修改什么 | 阅读文档 | 内容边界 |
| --- | --- | --- |
| 登录、OAuth、回调、TokenStore、CLI | [login.md](login.md) | 从 App 配置到 `/v0/me` 验证的完整登录事务 |
| 功能声明、八项权限、动态指导、权限不足交互 | [capabilities.md](capabilities.md) | `BangumiCapability`、`BangumiModuleOptions` 与 remediation 契约 |
| 搜索条目 | [search.md](search.md) | 公开 POST 搜索、可选账号上下文、筛选、分页与 DTO |
| 每日放送 | [calendar.md](calendar.md) | 公开 GET 时间表、七日 DTO、校验、UI 与缓存边界 |
| 条目详情 | [subject.md](subject.md) | 公开 GET 完整 Subject、校验、Catalog 落库与本地优先边界 |
| 读取章节 | [episodes.md](episodes.md) | 公开 GET 分页章节、DTO、校验与本地关联边界 |
| 获取用户收藏 | [collections.md](collections.md) | 官方端点、分页、DTO、解析、权限语义与首个接口 |
| 应用架构和目录边界 | [../arch.md](../arch.md) | View、Presentation、Model、Runtime 与迁移关系 |
| 项目整体路线 | [../plan.md](../plan.md) | 播放器、数据层、同步和应用级规划 |

## 当前实现快照

- 浏览器 OAuth Authorization Code 流程、回环回调、随机 `state`、超时和 Token 交换。
- `/v0/me` 身份验证，登录成功后才提交 Token，并在首次交互输入时才保存 App ID/App Secret。
- 缺少 App 参数时展示个人应用创建链接、准确回调地址、最低权限与可选权限。
- 打开浏览器前远程预检授权页，可识别已观察到的 `app_nonexistence`；Token 交换阶段补充 App Secret/回调错误提示。
- 八项 Bangumi 权限使用位枚举表示；功能通过初始化 options 声明自己依赖的权限。
- 公开条目搜索支持完整筛选和分页；未登录时匿名请求，活动会话存在时使用可选 Token，不注册或检查 capability。
- 公开每日放送支持匿名 `GET /calendar`，将旧版七日响应映射为类型化 DTO，并校验七个唯一星期和条目基本字段。
- 公开条目详情支持可选认证 `GET /v0/subjects/{id}`；每日放送点击会先抓取完整 Subject 与全部章节并写入 CatalogStore，之后详情页只读取数据库。
- 公开章节读取支持匿名 `GET /v0/episodes` 和最多 200 条分页；当前由媒体关联流程消费并持久化为本地 Episode 快照。
- `BangumiCalendarViewModel` 已将真实时间表接入首页今日摘要与 Bangumi 默认页签；加载、错误、刷新和星期选择不进入 QML JavaScript 业务逻辑。
- `BangumiBrowserViewModel` 已将公开搜索、账户恢复/登录/退出和收藏分页接入 QML；CLI 由独立二进制提供高级筛选与终端输出。
- `search` CLI 命令支持位置关键词、类型、排序、标签与分页；尽力复用已保存会话，恢复失败时继续匿名查询。
- `MemoryTokenStore`、`FileTokenStore` 和默认的 `SystemTokenStore`；系统后端覆盖 Linux Secret Service、Windows Credential Manager 与 macOS Keychain。
- 首个能力参考切片：读取当前登录用户的收藏，包含过滤、分页、DTO 映射和结构化权限修复信息。
- `collections` CLI 命令恢复会话、发起一页 GET 查询；Client 校验 DTO 后，CLI 输出服务端原始 JSON。

尚未完成：

- 真实 Bangumi 账号的端到端人工验收。
- Refresh Token 自动刷新。
- 收藏写入、章节进度写入、缓存与同步。

## 跨专题约束

1. View 不直接调用 Auth、Client 或 TokenStore；CLI 通过 Presenter，QML 通过 Presenter/ViewModel，业务入口统一进入 `BangumiModule`。
2. Model 不读取 argv、不打印 stdout，也不把 OAuth JSON 暴露给上层。
3. Qt 对象留在创建线程；异步流程使用 Ilias Task 和 Qt Awaiter，不在事件循环中 `.wait()`。
4. Token 只进入 TokenStore，不进入普通设置或业务数据库；Client Secret 在设置文件中加密存储。
5. 错误文本和日志不得包含 token、code、client secret、完整回调 URL 或原始 Token 响应。
6. 功能声明只提供“需要什么权限”的用户指导；最终能否执行始终以 Bangumi API 的实际响应为准。
7. 新增外部协议事实时，应写入对应专题文档并附官方来源，不让高频知识只能靠反复翻阅远端文档。
8. 公开 API 不为统一形式强行注册 `requiredCapabilities=None` 的 feature；可以使用活动会话，但不得把登录变成前置条件。

## 运行日志

默认日志级别为 `info`，覆盖应用启动/退出、设置加载、凭据后端、登录状态迁移、OAuth 阶段、HTTP 请求结果和收藏操作结果。所有 CLI 命令可通过 `--log-level` 切换为 `trace`、`debug`、`info`、`warn`、`error` 或 `critical`；未启用 spdlog 时，消息仍使用 `std::format` 语法格式化，再交给 `qDebug`、`qInfo`、`qWarning` 或 `qCritical`。两个二进制默认使用接近 spdlog 的“时间、级别、文件:行号、正文”Qt 消息模板，显式的 `QT_MESSAGE_PATTERN` 环境变量可覆盖它；`ANIME_LAND_LOG_LEVEL` 也可设置级别，CLI 命令行参数优先。

日志只使用固定路由模板和结构化元数据。禁止记录 token、授权码、client secret、OAuth state、完整回调 URL、原始 Token 响应和收藏响应正文；调试新增日志时也必须遵守这一边界。

## 渐进路线

原设计把收藏全部放在 Step 4。本次根据明确需求提前实现一个只读纵向切片，用于验证能力声明和权限交互 API；这不代表提前展开收藏写入、缓存或同步。

```mermaid
flowchart LR
    L[登录闭环] --> R[会话刷新]
    L --> CR[Collection Read<br/>能力参考切片]
    R --> Q[更多只读 API]
    CR --> Q
    Q --> CW[收藏与章节写入]
    CW --> S[缓存与同步]
```

## 文件导航

| 文件 | 职责 |
| --- | --- |
| `src/model/bangumi/auth.*` | OAuth、预检、回调和 Token 交换 |
| `src/model/bangumi/capability.*` | 权限枚举元数据、功能声明、动态指导和修复错误 |
| `src/model/bangumi/search.*` | 公开条目搜索请求、响应 DTO、编码与校验 |
| `src/model/bangumi/calendar.*` | 公开每日放送请求、七日 DTO 与响应校验 |
| `src/model/bangumi/subject.*` | 公开条目详情请求、Subject DTO 语义别名与响应校验 |
| `src/model/bangumi/episode.*` | 公开分页章节请求、DTO 与响应校验 |
| `src/model/bangumi/collection.*` | 收藏查询值对象、DTO、URL 和 JSON 映射 |
| `src/model/bangumi/client.*` | `/v0/me`、公开搜索、每日放送、条目详情、章节与收藏 HTTP 请求 |
| `src/model/bangumi/bangumi.*` | Model 门面、状态和事务编排 |
| `src/model/bangumi/config.*` | Token、错误模型和 TokenStore |
| `src/presentation/bangumi/*` | 与具体前端无关的 Presenter 和 View 契约 |
| `src/view/cli/*` | CLI 参数、命令分派、退出码和具体 View |

Qt Quick/QML 的每日放送和条目详情已接真实 Presentation ViewModel；搜索、收藏等页面仍使用 fixture。
设置 `ANIME_LAND_UI_FIXTURE=1` 可令整个图形界面回到无网络的独立 View 调试模式。后续边界和迁移状态见[应用架构](../arch.md)。

## 外部依据

- [Bangumi 开发者应用](https://bgm.tv/dev/app)
- [Bangumi API 文档](https://bangumi.github.io/api/)
- [Bangumi v0 OpenAPI](https://github.com/bangumi/api/blob/master/open-api/v0.yaml)
- [Bangumi 用户授权机制](https://github.com/bangumi/api/blob/master/docs-raw/How-to-Auth.md)
- [Bangumi User-Agent 建议](https://github.com/bangumi/api/blob/master/docs-raw/user%20agent.md)

外部协议核对日期：2026-07-26。
