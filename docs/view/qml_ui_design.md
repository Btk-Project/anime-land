# QML 主界面设计

## 1. 当前状态

Qt Quick 主界面已经以 fixture 形式落地。无参数启动应用进入 QML；现有 Bangumi CLI
子命令继续使用原来的参数入口。fixture 只验证页面结构、导航和交互状态，不访问
Bangumi、CatalogStore、LibraryStore 或 PlaybackSession。

当前目标是先冻结主要信息架构和公共组件，再由 Presentation 层逐页替换 fixture 数据。

## 2. 信息架构

顶层侧栏只保留四个入口：

- 首页：继续观看、最近加入和最近播放；
- 媒体库：本地搜索、观看状态筛选和媒体导入入口；
- Bangumi：公开搜索与账户收藏；
- 设置：账户、媒体资源、播放和应用设置。

条目详情和播放器是栈内二级页面，不占用顶层导航。媒体资源 provider、扫描任务和
批量关联属于媒体库的二级管理界面，后续不扩张为顶层入口。

## 3. 页面

| 页面 | 当前 fixture 行为 | 后续 Presentation 输入 |
| --- | --- | --- |
| 首页 | 继续观看主卡、最近加入、最近播放 | `HomeViewModel` |
| 媒体库 | 本地文字筛选、观看状态筛选、海报网格 | `LibraryViewModel` |
| Bangumi | 搜索/收藏切换、海报网格 | `BangumiViewModel` |
| 条目详情 | 元数据、播放/关联入口、章节列表 | `SubjectDetailsViewModel` |
| 播放器 | 视频输出占位、控制栏、章节队列 | `PlaybackViewModel` |
| 设置 | 四组设置入口和未接入提示 | `SettingsViewModel` |

媒体库与 Bangumi 复用 `SubjectCard`，本地和远端条目最终由 Presentation 映射为稳定的
UI DTO。QML 不接收 Bangumi 协议 DTO、文件路径或 provider descriptor。

## 4. 视觉约束

- 采用朴素深色桌面布局，不使用装饰性渐变；
- 使用灰阶面板层级和低饱和蓝灰强调色；
- 海报、视频和内容信息优先，按钮与描边保持克制；
- 基础间距为 8 px，常用圆角为 6 至 12 px；
- 基准窗口为 `1320×820`，最小窗口为 `960×640`；
- `Theme.qml` 统一颜色、间距、字号和圆角；
- `AppText`、`AppButton` 等公共组件统一文字与控件行为，页面不自行复制主题规则。

## 5. 目录与边界

```text
src/view/qml/
├─ Main.qml                 # ApplicationWindow、侧栏与页面栈
├─ Theme.qml                # 视觉 token
├─ FixtureData.qml          # 仅供当前 UI 原型使用
├─ AppText.qml
├─ AppButton.qml
├─ NavigationItem.qml
├─ SubjectCard.qml
├─ EpisodeRow.qml
├─ *Page.qml
└─ qml_application.*        # QML 引擎和静态资源入口
```

QML 只负责布局、导航、局部选择状态和动画。加载、取消、分页、错误映射、业务命令和列表
模型由 Presentation 的 QObject ViewModel 提供。fixture 被真实数据替换时不得把网络、
SQL 或媒体控制逻辑搬入 QML JavaScript。

## 6. 启动与验证

- `xmake run main`：启动 fixture QML 主界面；
- `xmake run main --help`：验证原 CLI 入口；
- `ANIME_LAND_UI_SMOKE_TEST=1`：自动遍历六个页面并退出；
- `ANIME_LAND_UI_SCREENSHOT=<path>`：渲染首页并保存诊断截图。

Windows 离屏平台配合软件 Qt Quick backend 时可能无法正确栅格化系统字体，但组件加载、
绑定错误和导航仍可由 smoke test 检查。正常桌面平台使用系统字体渲染。

## 7. 下一步

1. 增加 QML 专用的 QObject ViewModel，不修改 CLI 的 View 合约；
2. 先用真实 Bangumi 搜索替换 `BangumiPage` fixture；
3. LibraryStore 完成后替换媒体库、详情章节和关联状态；
4. PlaybackSession 与 VideoOutputItem 完成后替换播放器占位区域；
5. 为公共组件添加 QML fixture 截图与键盘导航测试。
