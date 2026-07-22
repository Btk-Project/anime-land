#include "bangumi/config.hpp"
#include "bangumi/protocol.hpp"
#include "bangumi/system_credential_provider_p.hpp"
#include "common/log.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <sodium.h>

#include <limits>
#include <utility>

namespace anime_land {
namespace {

constexpr qint64 kMaximumCredentialFileSize = 64 * 1024;

auto invalidStoredCredential(QString message)
    -> BangumiResult<std::optional<BangumiToken>> {
  return ilias::Err(
      bangumiError(BangumiErrorCode::CredentialStoreError, std::move(message)));
}

/**
 * @brief 解码并验证本地持久化的 BangumiToken。
 * @pre data 是已通过凭据文件大小限制的完整 body。
 * @post 成功时直接返回公开 BangumiToken，不建立中间 wire DTO。
 */
auto decodeStoredToken(const QByteArray &data)
    -> BangumiResult<std::optional<BangumiToken>> {
  BangumiToken token;
  if (auto error = bangumi_protocol::decode(data, token)) {
    return invalidStoredCredential(
        QStringLiteral("保存的凭据不是有效的 JSON：%1").arg(*error));
  }
  if (token.accessToken.isEmpty() || token.refreshToken.isEmpty() ||
      token.tokenType.isEmpty() || token.userId < 0 || token.expiresAt < 0) {
    return invalidStoredCredential(
        QStringLiteral("保存的凭据缺少必需字段或字段类型不正确"));
  }
  return std::optional<BangumiToken>{std::move(token)};
}

/**
 * @brief 编码公开 BangumiToken 作为本地持久化 body。
 * @pre token 已通过认证流程验证，敏感字段不得记录到日志。
 * @post 不修改 token；成功返回拥有自身数据的紧凑 UTF-8 body。
 */
auto encodeStoredToken(const BangumiToken &token) -> BangumiResult<QByteArray> {
  auto data = bangumi_protocol::encode(token);
  if (!data) {
    return ilias::Err(
        bangumiError(BangumiErrorCode::CredentialStoreError,
                     QStringLiteral("无法把 Bangumi 凭据序列化为 Qt JSON")));
  }
  return std::move(*data);
}

class MemoryTokenStore final : public TokenStore {
public:
  ~MemoryTokenStore() override {
    if (mToken) {
      clearBangumiToken(*mToken);
    }
  }

  auto load()
      -> ilias::Task<BangumiResult<std::optional<BangumiToken>>> override {
    AL_LOG_DEBUG("[bangumi.credentials] load backend=memory present={}",
                 mToken.has_value());
    co_return mToken;
  }

  auto save(const BangumiToken &token)
      -> ilias::Task<BangumiResult<void>> override {
    if (mToken) {
      clearBangumiToken(*mToken);
    }
    mToken = token;
    AL_LOG_INFO("[bangumi.credentials] save completed backend=memory");
    co_return BangumiResult<void>{};
  }

  auto clear() -> ilias::Task<BangumiResult<void>> override {
    if (mToken) {
      clearBangumiToken(*mToken);
      mToken.reset();
    }
    AL_LOG_INFO("[bangumi.credentials] clear completed backend=memory");
    co_return BangumiResult<void>{};
  }

private:
  std::optional<BangumiToken> mToken;
};

class FileTokenStore final : public TokenStore {
public:
  explicit FileTokenStore(QString path) : mPath(std::move(path)) {}

  auto load()
      -> ilias::Task<BangumiResult<std::optional<BangumiToken>>> override {
    // QFile access is small but still blocking; keep it off the Qt event loop.
    co_return co_await ilias::blocking([path = mPath]() {
      AL_LOG_DEBUG("[bangumi.credentials] load started backend=file path={}",
                   path.toStdString());
      QFile file(path);
      if (!file.exists()) {
        AL_LOG_DEBUG("[bangumi.credentials] no credential backend=file");
        return BangumiResult<std::optional<BangumiToken>>{
            std::optional<BangumiToken>{}};
      }
      if (!file.open(QIODevice::ReadOnly)) {
        return invalidStoredCredential(QStringLiteral("无法读取凭据文件 %1：%2")
                                           .arg(path, file.errorString()));
      }
      if (file.size() <= 0 || file.size() > kMaximumCredentialFileSize) {
        return invalidStoredCredential(
            QStringLiteral("凭据文件大小无效：%1 bytes").arg(file.size()));
      }
      auto decoded = decodeStoredToken(file.readAll());
      if (decoded) {
        AL_LOG_INFO("[bangumi.credentials] load completed backend=file");
      } else {
        AL_LOG_ERROR("[bangumi.credentials] decode failed backend=file");
      }
      return decoded;
    });
  }

  auto save(const BangumiToken &token)
      -> ilias::Task<BangumiResult<void>> override {
    auto serialized = encodeStoredToken(token);
    if (!serialized) {
      AL_LOG_ERROR("[bangumi.credentials] encode failed backend=file");
      co_return ilias::Err(std::move(serialized.error()));
    }
    co_return co_await ilias::blocking([path = mPath, data = *serialized]() {
      AL_LOG_DEBUG("[bangumi.credentials] save started backend=file path={}",
                   path.toStdString());
      const QFileInfo info(path);
      QDir directory = info.dir();
      if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        return BangumiResult<void>{ilias::Err(bangumiError(
            BangumiErrorCode::CredentialStoreError,
            QStringLiteral("无法创建凭据目录：%1").arg(directory.path())))};
      }

      QSaveFile file(path);
      file.setDirectWriteFallback(false);
      if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
          !file.commit()) {
        return BangumiResult<void>{
            ilias::Err(bangumiError(BangumiErrorCode::CredentialStoreError,
                                    QStringLiteral("无法保存凭据文件 %1：%2")
                                        .arg(path, file.errorString())))};
      }

      // File storage is deliberately transparent, but it should still default
      // to owner-only access instead of inheriting permissive permissions.
      if (!QFile::setPermissions(path, QFileDevice::ReadOwner |
                                           QFileDevice::WriteOwner)) {
        QFile::remove(path);
        return BangumiResult<void>{ilias::Err(bangumiError(
            BangumiErrorCode::CredentialStoreError,
            QStringLiteral("无法限制凭据文件权限，已删除该文件：%1")
                .arg(path)))};
      }
      AL_LOG_INFO("[bangumi.credentials] save completed backend=file");
      return BangumiResult<void>{};
    });
  }

  auto clear() -> ilias::Task<BangumiResult<void>> override {
    co_return co_await ilias::blocking([path = mPath]() {
      if (!QFile::exists(path) || QFile::remove(path)) {
        AL_LOG_INFO("[bangumi.credentials] clear completed backend=file");
        return BangumiResult<void>{};
      }
      return BangumiResult<void>{ilias::Err(
          bangumiError(BangumiErrorCode::CredentialStoreError,
                       QStringLiteral("无法删除凭据文件：%1").arg(path)))};
    });
  }

private:
  QString mPath;
};

/**
 * @brief 通过 SystemCredentialProvider 持久化 BangumiToken。
 * @invariant mProvider 非空并由本对象独占；所有平台调用都在阻塞线程中执行。
 */
class SystemTokenStore final : public TokenStore {
public:
  /**
   * @brief 构造系统 TokenStore。
   * @param provider 要独占的平台 provider。
   * @pre provider 非空。
   * @post 本对象获得 provider 的唯一所有权。
   */
  explicit SystemTokenStore(
      std::unique_ptr<detail::SystemCredentialProvider> provider)
      : mProvider(std::move(provider)) {}

  /** @copydoc TokenStore::load() */
  auto load()
      -> ilias::Task<BangumiResult<std::optional<BangumiToken>>> override {
    AL_LOG_DEBUG("[bangumi.credentials] load started backend=system");
    auto raw = co_await ilias::blocking([this]() { return mProvider->load(); });
    if (!raw) {
      AL_LOG_ERROR("[bangumi.credentials] load failed backend=system");
      co_return ilias::Err(std::move(raw.error()));
    }
    if (!*raw) {
      AL_LOG_DEBUG("[bangumi.credentials] no credential backend=system");
      co_return std::optional<BangumiToken>{};
    }

    auto result = decodeStoredToken(**raw);
    if (!(**raw).isEmpty()) {
      sodium_memzero((**raw).data(), static_cast<std::size_t>((**raw).size()));
      (**raw).clear();
    }
    if (result) {
      AL_LOG_INFO("[bangumi.credentials] load completed backend=system");
    } else {
      AL_LOG_ERROR("[bangumi.credentials] decode failed backend=system");
    }
    co_return result;
  }

  /** @copydoc TokenStore::save(const BangumiToken &) */
  auto save(const BangumiToken &token)
      -> ilias::Task<BangumiResult<void>> override {
    auto serialized = encodeStoredToken(token);
    if (!serialized) {
      AL_LOG_ERROR("[bangumi.credentials] encode failed backend=system");
      co_return ilias::Err(std::move(serialized.error()));
    }

    auto saved = co_await ilias::blocking(
        [this, data = std::move(*serialized)]() mutable {
          auto result = mProvider->save(data);
          if (!data.isEmpty()) {
            sodium_memzero(data.data(), static_cast<std::size_t>(data.size()));
            data.clear();
          }
          return result;
        });
    if (saved) {
      AL_LOG_INFO("[bangumi.credentials] save completed backend=system");
    } else {
      AL_LOG_ERROR("[bangumi.credentials] save failed backend=system");
    }
    co_return saved;
  }

  /** @copydoc TokenStore::clear() */
  auto clear() -> ilias::Task<BangumiResult<void>> override {
    auto cleared =
        co_await ilias::blocking([this]() { return mProvider->clear(); });
    if (cleared) {
      AL_LOG_INFO("[bangumi.credentials] clear completed backend=system");
    } else {
      AL_LOG_ERROR("[bangumi.credentials] clear failed backend=system");
    }
    co_return cleared;
  }

private:
  std::unique_ptr<detail::SystemCredentialProvider> mProvider;
};

} // namespace

auto detail::createSystemTokenStore(
    std::unique_ptr<SystemCredentialProvider> provider)
    -> std::unique_ptr<TokenStore> {
  return std::make_unique<SystemTokenStore>(std::move(provider));
}

auto bangumiError(BangumiErrorCode code, QString message) -> BangumiError {
  return BangumiError{
      .code = code, .message = std::move(message), .remediation = std::nullopt};
}

auto bangumiErrorCodeName(BangumiErrorCode code) -> std::string_view {
  switch (code) {
  case BangumiErrorCode::InvalidConfiguration:
    return "invalid_configuration";
  case BangumiErrorCode::InvalidState:
    return "invalid_state";
  case BangumiErrorCode::CallbackListenFailed:
    return "callback_listen_failed";
  case BangumiErrorCode::BrowserOpenFailed:
    return "browser_open_failed";
  case BangumiErrorCode::AuthorizationDenied:
    return "authorization_denied";
  case BangumiErrorCode::InvalidCallback:
    return "invalid_callback";
  case BangumiErrorCode::CallbackTimeout:
    return "callback_timeout";
  case BangumiErrorCode::NetworkError:
    return "network_error";
  case BangumiErrorCode::InvalidResponse:
    return "invalid_response";
  case BangumiErrorCode::Unauthorized:
    return "unauthorized";
  case BangumiErrorCode::MissingCapability:
    return "missing_capability";
  case BangumiErrorCode::UnsupportedCredentialStore:
    return "unsupported_credential_store";
  case BangumiErrorCode::CredentialStoreError:
    return "credential_store_error";
  case BangumiErrorCode::Cancelled:
    return "cancelled";
  case BangumiErrorCode::NotLoggedIn:
    return "not_logged_in";
  case BangumiErrorCode::TokenExpired:
    return "token_expired";
  }
  return "unknown";
}

auto parseTokenStoreKind(std::string_view name)
    -> BangumiResult<TokenStoreKind> {
  if (name == "memory") {
    return TokenStoreKind::Memory;
  }
  if (name == "file") {
    return TokenStoreKind::File;
  }
  if (name == "system") {
    return TokenStoreKind::System;
  }
  AL_LOG_WARN("[bangumi.credentials] unknown backend name={}", name);
  return ilias::Err(
      bangumiError(BangumiErrorCode::InvalidConfiguration,
                   QStringLiteral("未知的 TokenStore：%1")
                       .arg(QString::fromUtf8(
                           name.data(), static_cast<qsizetype>(name.size())))));
}

auto TokenStore::create(TokenStoreOptions options)
    -> BangumiResult<std::unique_ptr<TokenStore>> {
  if (options.kind != TokenStoreKind::File && !options.filePath.isEmpty()) {
    return ilias::Err(bangumiError(
        BangumiErrorCode::InvalidConfiguration,
        QStringLiteral("--token-file 只能与 file TokenStore 一起使用")));
  }

  switch (options.kind) {
  case TokenStoreKind::Memory:
    AL_LOG_INFO("[bangumi.credentials] initializing backend=memory");
    return std::make_unique<MemoryTokenStore>();
  case TokenStoreKind::File:
    if (options.filePath.isEmpty()) {
      options.filePath =
          QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
          QStringLiteral("/credentials.json");
    }
    AL_LOG_INFO("[bangumi.credentials] initializing backend=file");
    return std::make_unique<FileTokenStore>(std::move(options.filePath));
  case TokenStoreKind::System:
    AL_LOG_INFO("[bangumi.credentials] initializing backend=system");
    auto provider = detail::createPlatformSystemCredentialProvider();
    if (!provider) {
      AL_LOG_ERROR("[bangumi.credentials] system backend unavailable code={}",
                   bangumiErrorCodeName(provider.error().code));
      return ilias::Err(std::move(provider.error()));
    }
    return detail::createSystemTokenStore(std::move(*provider));
  }
  return ilias::Err(bangumiError(BangumiErrorCode::InvalidConfiguration,
                                 QStringLiteral("无效的 TokenStore 类型")));
}

void clearBangumiToken(BangumiToken &token) noexcept {
  auto clear = [](QString &value) {
    if (!value.isEmpty()) {
      sodium_memzero(value.data(),
                     static_cast<std::size_t>(value.size()) * sizeof(QChar));
      value.clear();
      value.squeeze();
    }
  };
  clear(token.accessToken);
  clear(token.refreshToken);
  clear(token.tokenType);
  if (token.scope) {
    clear(*token.scope);
    token.scope.reset();
  }
  token.userId = 0;
  token.expiresAt = 0;
}

void clearBangumiOAuthApplication(
    BangumiOAuthApplication &application) noexcept {
  auto clear = [](std::string &value) {
    if (!value.empty()) {
      sodium_memzero(value.data(), value.size());
      value.clear();
      value.shrink_to_fit();
    }
  };
  application.clientId.clear();
  application.clientId.squeeze();
  clear(application.clientSecret);
}

} // namespace anime_land
