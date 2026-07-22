#pragma once

#include "bangumi/config.hpp"

#include <QByteArray>

#include <memory>
#include <optional>

namespace anime_land::detail {

/**
 * @brief 系统凭据库的原始字节访问边界。
 *
 * provider 不解释 Bangumi 协议，也不决定 JSON 格式；它只负责以平台原生的
 * 凭据 API 保存、读取和删除一块不透明的敏感数据。该边界使 TokenStore 的协议
 * 逻辑可以在不访问用户真实钥匙串的条件下测试。
 *
 * @invariant 实现不得把凭据内容写入日志、错误消息或普通文件。
 * @invariant 实现不得在系统凭据库失败时降级到 FileTokenStore。
 */
class SystemCredentialProvider {
public:
  /**
   * @brief 释放平台 provider。
   * @pre 不得再有以该对象为目标的并发调用。
   * @post provider 持有的平台资源均已释放；系统中已经保存的凭据不受影响。
   */
  virtual ~SystemCredentialProvider() = default;

  /**
   * @brief 从系统凭据库读取 Bangumi 凭据 body。
   * @pre 调用线程允许执行阻塞的平台凭据 API；调用方负责串行访问 provider。
   * @post 找不到条目时返回空 optional；成功找到时返回拥有自身数据的 body；
   *       失败时返回 CredentialStoreError，且不创建或修改任何凭据。
   */
  virtual auto load() -> BangumiResult<std::optional<QByteArray>> = 0;

  /**
   * @brief 把 Bangumi 凭据 body 保存到系统凭据库。
   * @param data 要保存的不透明 UTF-8 body。
   * @pre data 非空且不超过 TokenStore 接受的最大凭据大小；调用方负责串行访问。
   * @post 成功时具有相同应用标识的旧条目被原子替换或更新；失败时返回
   *       CredentialStoreError，并且不得改用普通文件保存。
   */
  virtual auto save(const QByteArray &data) -> BangumiResult<void> = 0;

  /**
   * @brief 删除系统凭据库中的 Bangumi 凭据。
   * @pre 调用线程允许执行阻塞的平台凭据 API；调用方负责串行访问 provider。
   * @post 条目原本不存在或已全部删除时成功；其他失败返回 CredentialStoreError。
   */
  virtual auto clear() -> BangumiResult<void> = 0;
};

/**
 * @brief 创建当前平台的系统凭据 provider。
 * @pre 当前进程已经完成 Qt 核心应用初始化。
 * @post 受支持平台返回 provider；编译目标不受支持时返回
 *       UnsupportedCredentialStore。运行时服务是否可用由 provider 操作报告。
 */
auto createPlatformSystemCredentialProvider()
    -> BangumiResult<std::unique_ptr<SystemCredentialProvider>>;

/**
 * @brief 用指定 provider 创建 SystemTokenStore。
 * @param provider 获得唯一所有权的平台字节 provider。
 * @pre provider 非空，且其生命周期内不会被其他所有者访问。
 * @post 返回的 TokenStore 获得 provider 所有权，并通过它完成后续系统凭据操作。
 */
auto createSystemTokenStore(std::unique_ptr<SystemCredentialProvider> provider)
    -> std::unique_ptr<TokenStore>;

} // namespace anime_land::detail
