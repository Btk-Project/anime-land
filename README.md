# anime-land

## Qt 版本

- 源码兼容边界为 Qt 6.2 至 Qt 6.x；当前本机验证版本为 Qt 6.8.2。
- xmake 通常会自动检测 apt 安装的 Qt。需要显式选择时，传 SDK 根目录：

  ```bash
  xmake f --qt=/usr
  ```

## JSON 后端

- Bangumi HTTP 与凭据文件协议使用 neko-proto-tools 的 RapidJSON 后端。
- Qt JSON adapter 暂时保留，用于兼容性测试和后端对照，不参与 Bangumi 业务链路。

## 开发规范

- [C++ 代码规范](docs/coding-style.md)
