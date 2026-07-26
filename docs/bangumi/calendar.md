# Bangumi 每日放送

> 状态：Model、Calendar ViewModel 与真实 QML 接线已实现  
> 端点：`GET https://api.bgm.tv/calendar`  
> 鉴权：公开接口，不要求登录或 capability

## 1. 产品位置

每日放送属于远端 Bangumi 浏览能力，不属于本地媒体库：

- Bangumi 顶层页面默认展示 `每日放送`，并与 `搜索`、`我的收藏` 并列为页签；
- 页面使用星期选择器和海报网格，不在桌面宽度内压缩成七列；
- 首页只展示紧凑的`今日放送`摘要，并跳转到 Bangumi 的完整时间表；
- 不增加新的侧栏顶层入口，也不把时间表写入 `LibraryStore`。

正常启动时，QML 只消费 `BangumiCalendarViewModel` 提供的 UI DTO；不得直接持有本文件
定义的协议 DTO。设置 `ANIME_LAND_UI_FIXTURE=1` 后，应用不创建 BangumiModule、不加载
设置或发起网络请求，七日数据完全切回 `FixtureData.qml`，供 View 独立调试。

## 2. Model 契约

`BangumiModule::getCalendar()` 是公开业务入口，并委托
`BangumiClient::getCalendar()` 发起匿名 GET。请求具有以下约束：

- URL 从 `BangumiSettings::bangumi_api` 解析为 `/calendar`；
- 发送合法 User-Agent 和 `Accept: application/json`；
- 不发送 Authorization、Content-Type 或请求体；
- 响应大小限制为 8 MiB；
- 取消、网络错误、非 2xx、超限和 JSON/字段错误映射为结构化 `BangumiError`。

协议投影位于 `src/model/bangumi/calendar.*`：

```text
BangumiCalendar
└─ BangumiCalendarDay[7]
   ├─ weekday { id, en, cn, ja }
   └─ items[]
      └─ BangumiCalendarSubject
         ├─ id / type / name / nameCn / summary
         ├─ airDate / airWeekday
         └─ rating? / rank? / images? / collection?
```

旧端点还会返回评分直方图等当前页面不消费的字段。解析器忽略未知字段，只保留界面和
后续映射需要的稳定子集。

## 3. 响应校验

解析成功必须同时满足：

1. 顶层恰好包含七天；
2. 星期 ID 为 `1..7` 且不重复，三种星期标签非空；
3. 每个条目 ID 为正，`air_weekday` 位于 `1..7`，至少一个名称非空；
4. 可选评分、排名和在看人数不接受负值，评分不超过 10。

校验失败返回 `BangumiErrorCode::InvalidResponse`。不在 Model 中补齐缺失星期或伪造
条目，因为这会掩盖协议变化。

## 4. Presentation 与缓存

`presentation/bangumi/calendar_view_model.*` 负责：

- 选择当天并把七日 DTO 映射为稳定 UI DTO；
- 暴露 loading、error、selectedWeekday、七日摘要、选中日条目和今日摘要；
- 处理刷新取消、旧 generation 丢弃和错误文案；
- 为首页派生最多三个今日条目，而不是让首页再次请求接口；
- 在销毁时取消 Model 请求并等待页面任务结束，避免悬空协程。

真实 Calendar 响应中的 `collection.doing` 是全站在看人数，不是当前用户的收藏状态。
因此`只看在看`目前仅存在于 fixture 模式；真实模式要等账户收藏 overlay 接入后才能开放，
不得使用全站人数伪造个人筛选。

时间表是短期远端浏览数据。接入 Bangumi Cache 后采用短 TTL（建议 15–30 分钟）和
失败时陈旧回退；它不进入应用拥有的目录身份、章节关联或播放进度表。只有用户显式
导入/关联条目时，远端 Subject 才通过 Library 的映射边界进入本地目录。

## 5. 时间语义

该端点按星期返回条目，并不提供可靠的具体播出时刻、字幕更新时间或本地媒体可用性。
因此界面使用`今日放送`/`每日放送`，不显示虚构的几点更新，也不据此自动创建章节、
更新观看进度或触发媒体扫描。

## 6. 测试

`test_bangumi_foundation` 覆盖：

- 匿名公开请求的 URL、Header 和空请求体；
- 带可选字段和未知评分直方图的七日响应；
- 缺少星期与重复星期的拒绝；
- Client 使用 GET、无 Authorization，并返回类型化结果。

`test_calendar_view_model` 覆盖 DTO 到 QML 状态的映射、错误展示、重试和星期选择边界。

## 外部依据

- [Bangumi 每日放送页面](https://bangumi.tv/calendar)
- [Bangumi 每日放送 JSON](https://api.bgm.tv/calendar)
- [Bangumi API 文档](https://bangumi.github.io/api/)

外部协议核对日期：2026-07-26。
