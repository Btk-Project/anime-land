# QML 主界面设计

## 1. 当前状态

Qt Quick 主界面已经开始接入真实 Presentation：无参数启动时，每日放送通过
`BangumiCalendarViewModel` 请求真实数据；现有 Bangumi CLI 子命令继续使用原来的参数
入口。媒体库通过 `LibraryViewModel` 读取本地数据库，支持文件选择器显式导入、按父目录
组织文件、搜索 Bangumi 条目/章节、写入或解除手动关联，以及双击/右键调用系统播放器；
条目详情通过 `SubjectDetailsViewModel` 读取 Catalog/Library 数据库并按章节打开关联媒体。
Bangumi 主搜索页、收藏和内置播放器仍使用 fixture。

设置 `ANIME_LAND_UI_FIXTURE=1` 可切换到完全隔离的 View 调试模式。该模式不加载应用
设置、不创建 BangumiModule，也不访问 Bangumi、CatalogStore、LibraryStore 或
PlaybackSession。`ANIME_LAND_UI_SMOKE_TEST=1` 会自动启用此模式，保证页面遍历稳定。

当前目标是先冻结主要信息架构和公共组件，再由 Presentation 层逐页替换 fixture 数据。

## 2. 信息架构

顶层侧栏只保留四个入口：

- 首页：继续观看、今日放送摘要、最近加入和最近播放；
- 媒体库：本地搜索、观看状态筛选和媒体导入入口；
- Bangumi：每日放送、公开搜索与账户收藏；
- 设置：账户、媒体资源、播放和应用设置。

条目详情和播放器是栈内二级页面，不占用顶层导航。媒体资源 provider、扫描任务和
批量关联属于媒体库的二级管理界面，后续不扩张为顶层入口。

## 3. 页面

| 页面 | 当前 fixture 行为 | 后续 Presentation 输入 |
| --- | --- | --- |
| 首页 | 真实今日放送摘要；其余为 fixture | `BangumiCalendarViewModel`，后续 `HomeViewModel` |
| 媒体库 | fixture 下保留条目卡片；真实模式支持导入、父目录分组、关联/解除、系统播放和安全移除 | `LibraryViewModel`（已接入） |
| Bangumi | 真实每日放送；搜索/收藏仍为 fixture | `BangumiCalendarViewModel`，后续 `BangumiViewModel` |
| 条目详情 | fixture 模式保留静态预览；真实模式显示数据库元数据、章节、媒体关联和播放入口 | `SubjectDetailsViewModel`（已接入） |
| 播放器 | 视频输出占位、控制栏、章节队列 | `PlaybackViewModel` |
| 设置 | 四组设置入口和未接入提示 | `SettingsViewModel` |

Bangumi 条目继续使用 `SubjectCard`；真实本地文件使用更紧凑的 `MediaItemCard`，避免在
尚未建立章节关联时伪装成条目。两者都由 Presentation 映射为稳定 UI DTO。QML 不接收
Bangumi 协议 DTO、文件路径或 provider descriptor。

真实模式的“导入媒体”打开多文件选择器，只提交用户选中的本地 URL。ViewModel 管理
导入中、错误、成功提示和导入后刷新；当前不递归扫描目录，也不在 QML 中解析文件名。
同一父目录的文件显示在一个待整理资源组中，但目录组不伪装成季度或 Bangumi 条目。
组内 `MediaItemCard` 双击直接播放；右键提供“播放”“关联章节”“解除当前关联”和“从
媒体库移除”。关联 Dialog 搜索真实 Bangumi 动画，选中条目后读取章节，并以本地
EpisodeId 提交手动关联。确认移除只删除库记录、不删除磁盘原文件；最后一个子项移除后，
空目录组随重新加载消失。QML 不接收真实文件路径，播放仍以 SourceItemId 调用 Model。

已关联卡片的右键菜单可进入条目详情。详情页只接收本地 `SubjectId`，通过
`SubjectDetailsViewModel` 展示 CatalogStore 的标题、简介、封面、标签和章节，并合并
LibraryStore 中每章关联的媒体显示名；章节播放只提交本地 `EpisodeId`。从每日放送进入时
只把 Bangumi ID 交给 Model：6 小时内的 Details 快照直接从 CatalogStore 显示；过期或
尚未落库的条目先读取完整 Subject 与全部章节并持久化，再重新读取数据库；远端失败时页面显示错误和
“重试读取”。任何路径都不会混入 `FixtureData.episodes`。播放进度尚未持久化，详情 DTO
中的进度目前固定为零。远端空章节标题显示为“标题待公布”；没有关联媒体时，禁用的
主操作文案为“暂无可播放媒体”，不显示可操作的“开始播放”。

每日放送是 Bangumi 的默认页签。页面以一行星期按钮切换七日内容，首页的`完整放送表`
直接切到该页；两处共享同一个 Calendar ViewModel，避免重复请求。`只看在看`依赖当前
用户收藏 overlay，现阶段只在 fixture 模式提供，真实模式不把全站统计误作个人状态。

## 4. 视觉约束

- 采用朴素深色桌面布局，不使用装饰性渐变；
- 使用灰阶面板层级和低饱和蓝灰强调色；
- 海报、视频和内容信息优先，按钮与描边保持克制；
- 基础间距为 8 px，常用圆角为 6 至 12 px；
- 基准窗口为 `1320×820`，最小窗口为 `960×640`；
- `Theme.qml` 统一颜色、间距、字号和圆角；
- `AppText`、`AppButton` 等公共组件统一文字与控件行为，页面不自行复制主题规则。
- `AppButton` 的禁用态统一使用 `Theme.textFaint` 和 `Theme.border`，即使是 primary 按钮也
  不继续使用只适合强调色背景的 `accentText`。
- `Menu`、`Dialog` 等独立 Popup 不依赖平台默认浅色背景，必须显式使用 `Theme` 的背景、
  描边和前景色，保证弹出层与主窗口深色主题一致。

## 5. 目录与边界

```text
src/view/qml/
├─ Main.qml                 # ApplicationWindow、侧栏与页面栈
├─ Theme.qml                # 视觉 token
├─ FixtureData.qml          # 长期保留的独立 View 调试数据
├─ AppText.qml
├─ AppButton.qml
├─ NavigationItem.qml
├─ SubjectCard.qml
├─ MediaItemCard.qml
├─ EpisodeRow.qml
├─ *Page.qml
└─ qml_application.*        # QML 引擎和静态资源入口
```

QML 只负责布局、导航、局部选择状态和动画。加载、取消、分页、错误映射、业务命令和列表
模型由 Presentation 的 QObject ViewModel 提供。fixture 被真实数据替换时不得把网络、
SQL 或媒体控制逻辑搬入 QML JavaScript。

## 6. 启动与验证

- `xmake run main`：启动 QML，并接入真实 Bangumi 每日放送和本地媒体库；
- `$env:ANIME_LAND_UI_FIXTURE='1'; xmake run main`：启动无网络的独立 View fixture；
- `Remove-Item Env:ANIME_LAND_UI_FIXTURE`：在当前 PowerShell 会话中恢复默认真实模式；
- `xmake run main --help`：验证原 CLI 入口；
- `ANIME_LAND_UI_SMOKE_TEST=1`：自动遍历六个页面并退出；
- `ANIME_LAND_UI_SCREENSHOT=<path>`：渲染首页并保存诊断截图。

Windows 离屏平台配合软件 Qt Quick backend 时可能无法正确栅格化系统字体，但组件加载、
绑定错误和导航仍可由 smoke test 检查。正常桌面平台使用系统字体渲染。

## 7. 下一步

1. 用真实 Bangumi 搜索替换 `BangumiPage` 的搜索 fixture；
2. 接入账户收藏，并为每日放送叠加当前用户的在看状态；
3. 把已关联媒体库主层级提升为 Subject；
4. 用 PlaybackSession/nekoav 替换当前系统默认播放器过渡入口和播放器占位区域；
5. 为公共组件添加 QML fixture 截图与键盘导航测试。
