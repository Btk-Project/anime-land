# 通用组件功能

## 1. 程序特殊路径配置

| 常量                 | 含义                         | 示例                                       |
| -------------------- | ---------------------------- | ------------------------------------------ |
| `${APP_INSTALL_DIR}` | 程序安装根目录               | `C:\Program Files\MyApp`                   |
| `${APP_EXEC_DIR}`    | 当前可执行文件所在目录       | `C:\Program Files\MyApp\bin`               |
| `${APP_CONFIG_DIR}`  | 程序实际使用的配置目录       | `C:\Users\Alice\AppData\Roaming\MyApp`     |
| `${APP_DATA_DIR}`    | 程序持久化数据目录           | `C:\Users\Alice\AppData\Local\MyApp\data`  |
| `${APP_LOG_DIR}`     | 日志目录                     | `C:\Users\Alice\AppData\Local\MyApp\logs`  |
| `${APP_CACHE_DIR}`   | 可删除、可重新生成的缓存目录 | `C:\Users\Alice\AppData\Local\MyApp\cache` |
| `${APP_TEMP_DIR}`    | 程序临时文件目录             | `/tmp/myapp`                               |
| `${WORK_DIR}`        | 程序当前工作目录             | 启动程序时的当前目录                       |
| `${USER_HOME_DIR}`   | 当前用户主目录               | `/home/alice`                              |
| `${APP_VERSION}`     | 程序版本                     | `1.0.0`                                    |
| `${APP_NAME}`        | 程序名称                     | `MyApp`                                    |
| `${USER_NAME}`       | 当前用户名                   | `Alice`                                    |
| `${HOST_NAME}`       | 当前主机名                   | `myhost`                                   |
