# Episode Provider 插件运行时与工作空间

> 状态：ABI v1 / 首个 C++ 实现已落地  
> 关联设计：[分集资源、JS Provider 与媒体缓存](online_source_design.md)  
> SDK：[TypeScript 声明](../../plugins/sdk/episode-provider-v1.d.ts)、
> [Manifest Schema](../../plugins/sdk/manifest-v1.schema.json)

## 1. 当前交付边界

当前代码已经实现：

- Model 层 `EpisodeProvider`、结构化查询、惰性 `OnlinePlayable`、Registry、
  `EpisodeResourceService` 和内存 TTL cache；
- 独立 `episode_provider_js` adapter target；
- 插件包 Manifest、默认配置与用户覆盖配置的加载、合并和边界检查；
- QJSEngine 注册、同步 JSON 状态机 ABI、libxml2 HTML recover parser 与 XPath 1.0 查询；
- QNetworkAccessManager Host：HTTPS origin 权限、手动重定向、方法/请求头白名单、超时、
  响应大小、单操作请求数、最小请求间隔、取消与镜像 failover；
- 内置 `org.anime-land.yhdmmm` 插件和完全脱网的搜索/详情/播放页夹具测试；
- GUI composition root 启动时加载内置插件，并扫描配置的用户插件目录，但不会自动 ping 或
  搜索；
- 详情页 `EpisodeResourcesViewModel` 与在线资源 Dialog：按当前章节选择 1～N 个 Provider，
  分源展示 Loading/Empty/Error/Result、模糊匹配番剧与分集信息，并用临时 handle 惰性
  resolve 后交给内置播放器。

在线源仍不参与 Bangumi/本地元数据搜索，也不建立独立资源库。页面打开不触发 Provider；
只有用户确认搜索时才访问所选来源。QML 不接收媒体 URL、请求头或插件 JSON data。

## 2. 三种工作空间

插件运行时不向 JS 暴露文件系统。Host 管理三个彼此分离的空间：

```text
只读插件包
├─ manifest.json
├─ index.js
├─ config.schema.json
├─ config.defaults.json
└─ icon/fixture 等只读资源（可选）

用户配置
└─ ${APP_CONFIG_DIR}/providers/<pluginId>.json

Host cache（插件不可直接遍历）
├─ 内存：搜索结果、continuation、解析后的短期 URL、镜像健康状态
└─ 磁盘：后续可选的 HTTP/媒体 cache；不是 Library 持久对象
```

应用设置中的默认发现配置为：

```toml
[plugin_settings]
enabled = true
scan_on_startup = true
load_builtin = true
allow_symlinks = false
plugins_directory = "${APP_DATA_DIR}/plugins"
provider_config_directory = "${APP_CONFIG_DIR}/providers"
max_packages = 64
```

每次正常启动都会创建并只读扫描：

```text
${plugins_directory}/episode-providers/<package-directory>/manifest.json
```

扫描只看一级子目录并按名称排序，最多处理 `max_packages` 个包。坏包、版本不兼容包和重复
plugin id 只产生一条 skipped 诊断，不影响其他包或应用启动。内置 plugin id 优先；除非把
`load_builtin` 关闭，否则用户目录中的同 id 包不会执行入口。默认拒绝符号链接，显式打开
`allow_symlinks` 只用于受信任的本地开发工作区。修改目录或安装包后重启应用即可重新发现。

内置包编进 Qt Resource，路径为：

```text
:/anime-land/plugins/episode-providers/<pluginId>/
```

开发时的源目录为：

```text
plugins/episode-providers/<pluginId>/
```

用户插件目录扫描已经实现，但它不是应用商店或安全沙箱。QJSEngine 不能强制中断同步死
循环，也没有可靠的每 engine 硬内存配额；在独立低权限进程隔离完成前，v1 只应放入用户
显式信任的插件。Manifest 权限是必要的能力边界，但不等价于完整安全沙箱。

## 3. 加载与生命周期

```text
load built-ins + scan configured directory
  → discover package
  → validate path/size/manifest/runtime version
  → load defaults + user override
  → create QJSEngine/HTML bridge/network manager
  → evaluate index.js
  → collect registrations
  → validate all Provider descriptors
  → atomically publish to Registry
  → ready
  → quiesce/cancel
  → invalidate provider cache
  → destroy engine and network manager
```

目录发现和插件加载都没有网络副作用。`ping`、`search`、`resolve` 只能由明确的应用用例触发。包内多个
Provider 必须全部验证通过才进入 Registry；`providerKey` 固定为
`<pluginId>.<providerId>`。有效配置与插件版本共同生成 runtime generation，配置变化后旧
cache key 不再命中。

同一个 QJSEngine 的操作串行执行；第二个并发调用返回 `busy`。网络 await、超时和取消由
C++/Ilias 执行，JS 只在每个网络响应后短时间同步运行。当前 engine 位于创建它的 Qt
composition 线程；把未审核插件开放给用户前，应将整个 runtime 移至专用进程，而不只是
QThread。

## 4. JSON 状态机 ABI

插件不能得到 `QNetworkAccessManager`、`QNetworkReply` 或其他原始 QObject。入口是普通
classic script，通过全局 Host 注册：

```js
AnimeLand.registerEpisodeProvider({
  id: "example",
  name: "Example",

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

每次 `begin()`/`resume()` 的返回值都先经过 `JSON.stringify`、大小限制和 C++ Schema
检查。合法 Step 只有三种：

- `request`：一个受 Host 约束的 HTTP 请求和下一次 continuation state；
- `complete`：操作的最终 JSON 值；
- `fail`：稳定错误文本以及是否允许切换到下一个镜像重试整条链。

state 必须是普通 JSON，不能包含 QObject、DOM node、函数、Promise、循环引用或二进制裸
指针。Host 每次切换镜像都会从 `begin()` 重新开始，禁止把镜像 A 的详情 ID 与镜像 B 的
页面混用。

### 4.1 操作

`ping` 输入为空，完成值为：

```json
{"reachable": true, "mirrorId": "primary", "detail": "HTTP 200"}
```

`search` 输入为结构化 `EpisodeQuery`，完成值为 `OnlinePlayable[]`。搜索结果允许是惰性的：
主视频 `data` 只保存 JSON continuation，不必已有 URL。

`resolve` 输入是用户选中的惰性 `OnlinePlayable`，完成值必须保持相同 `key`，并把唯一主视频
补全为可播放 URL 和准确的 `streamType`。Host 再检查 media origin、HTTPS 与 HLS m3u8
一致性。解析后的短期 URL 只进入内存 cache，不返回 QML、不写业务数据库。

## 5. HTML 能力

`AnimeLand.html.queryAll(html, rowXPath, fields)` 使用 libxml2 的 recover HTML parser 和
XPath 1.0：

```js
const rows = AnimeLand.html.queryAll(response.text,
  "//a[contains(@class, 'episode')]", {
    href: "string(@href)",
    title: "normalize-space(string(.))"
  });
```

`rowXPath` 先选择最多 256 个节点；`fields` 的每条 XPath 再相对该节点求字符串值。解析禁止
外部实体/网络访问，HTML 与字段数均有硬上限。document/node handle 不跨 JS 调用存活。

这提供了用户所说的 lxml 类能力，但实际 C++ 依赖是 libxml2；Python lxml 不能直接注册到
QJSEngine。

## 6. 网络与请求预算

Manifest 分开声明：

- `permissions.network`：搜索、详情、播放页等元数据请求 origin；
- `permissions.media`：Provider 最终可以返回给播放器的媒体 origin。

v1 只允许 HTTPS、GET/POST/HEAD，以及 `Accept`、`Content-Type`、同权限范围内的
`Referer`。Cookie jar、User-Agent、重定向、超时和代理由 Host 管理；JS 不能设置
Authorization/Cookie，也不能关闭 TLS 校验。跳转后的每个 origin 都重新检查权限。

`yhdmmm` 默认配置把请求预算控制为：

- 加载：0 请求；
- 用户显式 ping：1 个 HEAD；
- 用户显式搜索：1 个搜索页 + 最多 2 个候选详情页；
- 展开结果：0 请求；
- 用户选择播放：1 个播放页；
- 每个网络请求之间至少 750 ms；
- 单操作最多 6 请求，单响应最多 4 MiB。

空搜索结果是成功，不触发镜像 failover。只有网络/TLS/超时/5xx 等可重试错误才从下一镜像
重新开始，而且启动时不会为了“健康检查”轮询所有镜像。

## 7. yhdmmm 参考链路

参考插件实现以下公开页面结构：

```text
search(subjectName)
  POST /vodsearch.html (wd=<title>)
  → 最多 maxCandidates 个 /4kvideo/<id>.html
  → 匹配 /4kplay/<subject>-<line>-<episode>.html
  → 返回惰性 OnlinePlayable（不请求播放页）

resolve(selectedPlayable)
  GET selected /4kplay/...html
  → 解析 var player_data
  → encrypt == 0 且 HTTPS URL
  → from/url 识别 HLS
  → MediaStreamType::Hls + m3u8 URL
```

第一期把解析出的 m3u8 URL 交给现有 `PlaybackPipeline::open(QUrl)` / NekoAV PlayBin。
stream/AVIO cache API 仍只是后续扩展点。

## 8. 项目接入点与验证

构建关系：

```text
model
  └─ episode_resource/*

episode_provider_js (QtQml + QtNetwork + libxml2)
  ├─ depends on model
  └─ embeds built-in plugin qrc

main
  └─ depends on episode_provider_js
```

GUI 在 `GraphicalRuntime::runShell()` 创建 Registry/cache/Service、加载内置包、扫描用户目录
并逐包原子注册；日志中的 `network_requests=0` 用于确认启动无网络副作用。脱网端到端测试
使用合成 fixture，覆盖设置默认值、目录扫描与坏包隔离、加载、POST 搜索、XPath 详情匹配、
惰性 continuation、播放页解析和 HLS 类型保留：

```sh
xmake f -m debug --enable_tests=y
xmake build test_episode_provider
xmake run test_episode_provider
xmake build main
```

下一接入切片只需新增详情页 ViewModel：现有 `EpisodeResourceService` 搜索会先查 TTL cache；
UI 只拿 opaque handle 与安全展示元数据；播放时由 handle 找回 Provider、调用 `resolve()`，
校验后把 m3u8 QUrl 交给 PlaybackController。这个切片不应改变插件 ABI。
