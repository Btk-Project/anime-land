# yhdmmm Episode Provider

这是 Episode Provider ABI v1 的内置参考插件，公开站点入口默认为
`https://yhdmmm.com/`，备用镜像为 `https://www.yhdmmm.com/`。

插件采用三段式惰性链路：

1. `POST /vodsearch.html` 搜索番剧；
2. 只读取最多 `maxCandidates` 个详情页并匹配章节/线路；
3. 搜索结果保存 `/4kplay/...` JSON continuation，用户选择结果后才由
   `resolve()` 读取一次播放页并解析 `player_data.url`。

应用启动和插件加载不会发起网络请求。所有请求都经过 C++ Host 的 HTTPS origin
权限、手动重定向校验、超时、响应上限、单操作请求上限和最小请求间隔；JS 无法直接访问
`QNetworkAccessManager`、文件系统或进程环境。

该插件是解析接口示例，不保证第三方站点持续可用。页面结构变化应通过发布插件更新处理；
镜像、限速和候选数量由独立配置文件调整，不应直接修改 `index.js`。
