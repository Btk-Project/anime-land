# anime-land

## 获取源码

nekoav 以源码级 Git submodule 位于 `third_party/nekoav`，主仓库固定可复现提交，
`.gitmodules` 同时记录其跟踪分支为 `main`：

```bash
git clone --recurse-submodules <anime-land-url>
```

已有工作区补齐依赖：

```bash
git submodule update --init --recursive
```

需要同步开发 nekoav 时，在子模块中切到 `main`，正常提交/推送，再回到主仓库更新
gitlink：

```bash
git -C third_party/nekoav switch main
git -C third_party/nekoav pull --ff-only
git add third_party/nekoav
```

## Qt 版本

- 源码兼容边界为 Qt 6.2 至 Qt 6.x；当前本机验证版本为 Qt 6.8.2。
- xmake 通常会自动检测 apt 安装的 Qt。需要显式选择时，传 SDK 根目录：

  ```bash
  xmake f --qt=/usr
  ```

## JSON 后端

- Bangumi HTTP 与凭据文件协议使用 neko-proto-tools 的 RapidJSON 后端。
- Qt JSON adapter 暂时保留，用于兼容性测试和后端对照，不参与 Bangumi 业务链路。

## 项目文档

- [应用架构](docs/arch.md)
- [开发计划](docs/plan.md)
- [Bangumi 模块](docs/bangumi/README.md)
- [本地数据库设计](docs/database/local_database_design.md)
- [C++ 代码规范](docs/coding-style.md)
