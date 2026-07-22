#include "bangumi/system_credential_provider_p.hpp"

#include <QByteArray>
#include <QString>

#include <memory>
#include <optional>
#include <utility>

#if defined(Q_OS_LINUX)

#include "bangumi/secret_service_prompt_p.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QEventLoop>
#include <QList>
#include <QMap>
#include <QScopeGuard>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

#elif defined(Q_OS_WIN)

#define NOMINMAX
#include <wincred.h>
#include <windows.h>

#elif defined(Q_OS_MACOS)

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#endif

namespace anime_land::detail {
namespace {

constexpr qsizetype kMaximumCredentialSize = 64 * 1024;

/**
 * @brief 构造不包含敏感内容的系统凭据错误。
 * @param message 仅描述操作、平台服务和错误原因的文本。
 * @pre message 不得包含凭据 body。
 * @post 返回 CredentialStoreError，不修改系统凭据库。
 */
auto systemCredentialError(QString message) -> BangumiError {
  return bangumiError(BangumiErrorCode::CredentialStoreError,
                      std::move(message));
}

#if defined(Q_OS_LINUX)

constexpr auto kServiceName = "org.freedesktop.secrets";
constexpr auto kServicePath = "/org/freedesktop/secrets";
constexpr auto kServiceInterface = "org.freedesktop.Secret.Service";
constexpr auto kCollectionInterface = "org.freedesktop.Secret.Collection";
constexpr auto kItemInterface = "org.freedesktop.Secret.Item";
constexpr auto kSessionInterface = "org.freedesktop.Secret.Session";
constexpr auto kPromptInterface = "org.freedesktop.Secret.Prompt";
constexpr int kMethodTimeoutMilliseconds = 30'000;
constexpr int kPromptTimeoutMilliseconds = 120'000;

using SecretAttributes = QMap<QString, QString>;
using ObjectPaths = QList<QDBusObjectPath>;

/**
 * @brief Secret Service 协议的 `(oayays)` Secret 结构。
 * @invariant session 是当前 OpenSession 返回的路径；plain session 下 parameters
 * 为空。
 */
struct SecretServiceSecret {
  QDBusObjectPath session;
  QByteArray parameters;
  QByteArray value;
  QString contentType;
};

/**
 * @brief 把 SecretServiceSecret 编组为 D-Bus 结构。
 * @pre secret 满足 SecretServiceSecret 的不变量。
 * @post argument 追加一个完整的 `(oayays)` 值；secret 不变。
 */
auto operator<<(QDBusArgument &argument, const SecretServiceSecret &secret)
    -> QDBusArgument & {
  argument.beginStructure();
  argument << secret.session << secret.parameters << secret.value
           << secret.contentType;
  argument.endStructure();
  return argument;
}

/**
 * @brief 从 D-Bus 结构解组 SecretServiceSecret。
 * @pre argument 的当前位置是一个 `(oayays)` 值。
 * @post secret 被完整替换，argument 前进到结构末尾。
 */
auto operator>>(const QDBusArgument &argument, SecretServiceSecret &secret)
    -> const QDBusArgument & {
  argument.beginStructure();
  argument >> secret.session >> secret.parameters >> secret.value >>
      secret.contentType;
  argument.endStructure();
  return argument;
}

/**
 * @brief 注册本实现使用的 Secret Service D-Bus 类型。
 * @pre 可在任意线程调用。
 * @post 类型在进程中恰好完成至少一次注册，后续 QVariant 可安全编组。
 */
void registerSecretServiceTypes() {
  static const bool registered = []() {
    qDBusRegisterMetaType<SecretAttributes>();
    qDBusRegisterMetaType<ObjectPaths>();
    qDBusRegisterMetaType<SecretServiceSecret>();
    return true;
  }();
  Q_UNUSED(registered);
}

/** Secret Service SearchItems 返回的已解锁与锁定条目。 */
struct SearchResult {
  ObjectPaths unlocked;
  ObjectPaths locked;
};

/**
 * @brief 基于 freedesktop.org Secret Service 的 Linux 系统凭据 provider。
 *
 * 每次操作建立 plain Secret Service session；plain 表示 D-Bus 传输内不再叠加
 * 应用层加密，安全性依赖用户 session bus 的访问控制，与常见 libsecret 默认行为
 * 一致。锁定 collection/item 时会触发桌面环境提供的系统提示。
 *
 * @invariant 不缓存凭据 body；不把系统后端错误降级到普通文件。
 */
class SecretServiceCredentialProvider final : public SystemCredentialProvider {
public:
  /**
   * @brief 构造使用当前用户 session bus 的 provider。
   * @pre Qt 核心应用已初始化。
   * @post 只保存共享的 session bus 连接；尚不访问或创建系统凭据条目。
   */
  SecretServiceCredentialProvider()
      : mConnection(QDBusConnection::sessionBus()) {
    registerSecretServiceTypes();
  }

  /** @copydoc SystemCredentialProvider::load() */
  auto load() -> BangumiResult<std::optional<QByteArray>> override {
    auto session = openSession();
    if (!session) {
      return ilias::Err(std::move(session.error()));
    }
    auto close = qScopeGuard([this, path = *session]() { closeSession(path); });

    auto found = searchItems();
    if (!found) {
      return ilias::Err(std::move(found.error()));
    }
    if (found->unlocked.isEmpty() && !found->locked.isEmpty()) {
      auto unlocked = unlock(found->locked);
      if (!unlocked) {
        return ilias::Err(std::move(unlocked.error()));
      }
      found = searchItems();
      if (!found) {
        return ilias::Err(std::move(found.error()));
      }
    }
    if (found->unlocked.isEmpty()) {
      if (found->locked.isEmpty()) {
        return std::optional<QByteArray>{};
      }
      return ilias::Err(
          systemCredentialError(QStringLiteral("系统凭据条目仍处于锁定状态")));
    }

    auto reply = call(
        found->unlocked.front().path(), QString::fromLatin1(kItemInterface),
        QStringLiteral("GetSecret"), {QVariant::fromValue(*session)});
    if (!reply) {
      return ilias::Err(std::move(reply.error()));
    }
    if (reply->arguments().size() != 1) {
      return ilias::Err(systemCredentialError(
          QStringLiteral("Secret Service 的 GetSecret 响应格式无效")));
    }

    const auto secret =
        qdbus_cast<SecretServiceSecret>(reply->arguments().front());
    if (secret.value.isEmpty() ||
        secret.value.size() > kMaximumCredentialSize) {
      return ilias::Err(
          systemCredentialError(QStringLiteral("系统凭据大小无效：%1 bytes")
                                    .arg(secret.value.size())));
    }
    return std::optional<QByteArray>{secret.value};
  }

  /** @copydoc SystemCredentialProvider::save(const QByteArray &) */
  auto save(const QByteArray &data) -> BangumiResult<void> override {
    if (data.isEmpty() || data.size() > kMaximumCredentialSize) {
      return ilias::Err(systemCredentialError(
          QStringLiteral("拒绝保存大小无效的系统凭据：%1 bytes")
              .arg(data.size())));
    }

    auto session = openSession();
    if (!session) {
      return ilias::Err(std::move(session.error()));
    }
    auto close = qScopeGuard([this, path = *session]() { closeSession(path); });

    auto collection = defaultCollection();
    if (!collection) {
      return ilias::Err(std::move(collection.error()));
    }
    auto unlocked = unlock(ObjectPaths{*collection});
    if (!unlocked) {
      return ilias::Err(std::move(unlocked.error()));
    }

    QVariantMap properties;
    properties.insert(QStringLiteral("org.freedesktop.Secret.Item.Label"),
                      QStringLiteral("anime-land Bangumi OAuth token"));
    properties.insert(QStringLiteral("org.freedesktop.Secret.Item.Attributes"),
                      QVariant::fromValue(attributes()));
    const SecretServiceSecret secret{
        .session = *session,
        .parameters = {},
        .value = data,
        .contentType = QStringLiteral("application/json; charset=utf-8"),
    };
    auto reply =
        call(collection->path(), QString::fromLatin1(kCollectionInterface),
             QStringLiteral("CreateItem"),
             {properties, QVariant::fromValue(secret), true});
    if (!reply) {
      return ilias::Err(std::move(reply.error()));
    }
    if (reply->arguments().size() != 2) {
      return ilias::Err(systemCredentialError(
          QStringLiteral("Secret Service 的 CreateItem 响应格式无效")));
    }

    const auto prompt = qdbus_cast<QDBusObjectPath>(reply->arguments().at(1));
    auto prompted = completePrompt(prompt);
    if (!prompted) {
      return ilias::Err(std::move(prompted.error()));
    }
    return {};
  }

  /** @copydoc SystemCredentialProvider::clear() */
  auto clear() -> BangumiResult<void> override {
    auto found = searchItems();
    if (!found) {
      return ilias::Err(std::move(found.error()));
    }
    if (!found->locked.isEmpty()) {
      auto unlocked = unlock(found->locked);
      if (!unlocked) {
        return ilias::Err(std::move(unlocked.error()));
      }
      found = searchItems();
      if (!found) {
        return ilias::Err(std::move(found.error()));
      }
    }
    if (!found->locked.isEmpty()) {
      return ilias::Err(systemCredentialError(
          QStringLiteral("无法解锁要删除的系统凭据条目")));
    }

    for (const auto &item : found->unlocked) {
      auto reply = call(item.path(), QString::fromLatin1(kItemInterface),
                        QStringLiteral("Delete"), {});
      if (!reply) {
        return ilias::Err(std::move(reply.error()));
      }
      if (reply->arguments().size() != 1) {
        return ilias::Err(systemCredentialError(
            QStringLiteral("Secret Service 的 Delete 响应格式无效")));
      }
      auto prompted = completePrompt(
          qdbus_cast<QDBusObjectPath>(reply->arguments().front()));
      if (!prompted) {
        return ilias::Err(std::move(prompted.error()));
      }
    }
    return {};
  }

private:
  /**
   * @brief 返回用于唯一识别本应用条目的稳定属性。
   * @post 返回值不含 token、用户 ID 或其他敏感内容。
   */
  static auto attributes() -> SecretAttributes {
    return {
        {QStringLiteral("application"), QStringLiteral("anime-land")},
        {QStringLiteral("service"), QStringLiteral("bangumi")},
        {QStringLiteral("type"), QStringLiteral("oauth-token")},
    };
  }

  /**
   * @brief 调用一个 Secret Service 方法并统一翻译 D-Bus 错误。
   * @pre arguments 不含将被格式化到错误文本中的敏感内容。
   * @post 成功返回完整 reply；失败返回 CredentialStoreError。
   */
  auto call(const QString &path, const QString &interface,
            const QString &method, const QList<QVariant> &arguments)
      -> BangumiResult<QDBusMessage> {
    if (!mConnection.isConnected()) {
      return ilias::Err(systemCredentialError(
          QStringLiteral("无法连接当前用户的 D-Bus session：%1")
              .arg(mConnection.lastError().message())));
    }
    auto message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kServiceName), path, interface, method);
    message.setArguments(arguments);
    auto reply =
        mConnection.call(message, QDBus::Block, kMethodTimeoutMilliseconds);
    if (reply.type() == QDBusMessage::ErrorMessage) {
      return ilias::Err(
          systemCredentialError(QStringLiteral("Secret Service %1 失败（%2）")
                                    .arg(method, reply.errorName())));
    }
    return reply;
  }

  /**
   * @brief 建立 Secret Service plain session。
   * @pre session bus 可访问。
   * @post 成功返回需要由 closeSession() 关闭的 session 路径。
   */
  auto openSession() -> BangumiResult<QDBusObjectPath> {
    auto reply = call(QString::fromLatin1(kServicePath),
                      QString::fromLatin1(kServiceInterface),
                      QStringLiteral("OpenSession"),
                      {QStringLiteral("plain"),
                       QVariant::fromValue(QDBusVariant(QString{}))});
    if (!reply) {
      return ilias::Err(std::move(reply.error()));
    }
    if (reply->arguments().size() != 2) {
      return ilias::Err(systemCredentialError(
          QStringLiteral("Secret Service 的 OpenSession 响应格式无效")));
    }
    const auto path = qdbus_cast<QDBusObjectPath>(reply->arguments().at(1));
    if (path.path().isEmpty() || path.path() == QStringLiteral("/")) {
      return ilias::Err(systemCredentialError(
          QStringLiteral("Secret Service 未返回有效 session")));
    }
    return path;
  }

  /**
   * @brief 尽力关闭一次 Secret Service session。
   * @pre path 来自 openSession()。
   * @post 向服务发送 Close；关闭错误不覆盖原始操作结果。
   */
  void closeSession(const QDBusObjectPath &path) noexcept {
    auto message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kServiceName), path.path(),
        QString::fromLatin1(kSessionInterface), QStringLiteral("Close"));
    mConnection.call(message, QDBus::Block, kMethodTimeoutMilliseconds);
  }

  /**
   * @brief 按稳定属性查找应用创建的全部条目。
   * @post 成功返回互不重叠的 unlocked 与 locked 路径列表。
   */
  auto searchItems() -> BangumiResult<SearchResult> {
    auto reply = call(QString::fromLatin1(kServicePath),
                      QString::fromLatin1(kServiceInterface),
                      QStringLiteral("SearchItems"),
                      {QVariant::fromValue(attributes())});
    if (!reply) {
      return ilias::Err(std::move(reply.error()));
    }
    if (reply->arguments().size() != 2) {
      return ilias::Err(systemCredentialError(
          QStringLiteral("Secret Service 的 SearchItems 响应格式无效")));
    }
    return SearchResult{
        .unlocked = qdbus_cast<ObjectPaths>(reply->arguments().at(0)),
        .locked = qdbus_cast<ObjectPaths>(reply->arguments().at(1)),
    };
  }

  /**
   * @brief 解锁 collection 或 item，并完成可能出现的系统提示。
   * @param paths 要解锁的非空对象路径。
   * @pre paths 均来自当前 Secret Service。
   * @post 成功表示 Unlock 调用及其 prompt 已完成；调用方可重新查询锁定状态。
   */
  auto unlock(const ObjectPaths &paths) -> BangumiResult<void> {
    if (paths.isEmpty()) {
      return {};
    }
    auto reply = call(QString::fromLatin1(kServicePath),
                      QString::fromLatin1(kServiceInterface),
                      QStringLiteral("Unlock"), {QVariant::fromValue(paths)});
    if (!reply) {
      return ilias::Err(std::move(reply.error()));
    }
    if (reply->arguments().size() != 2) {
      return ilias::Err(systemCredentialError(
          QStringLiteral("Secret Service 的 Unlock 响应格式无效")));
    }
    auto prompted =
        completePrompt(qdbus_cast<QDBusObjectPath>(reply->arguments().at(1)));
    if (!prompted) {
      return ilias::Err(std::move(prompted.error()));
    }
    return {};
  }

  /**
   * @brief 获取默认 collection；缺失时请求 Secret Service 创建并绑定 default
   * alias。
   * @post 成功返回有效 collection 路径；用户取消创建时返回
   * CredentialStoreError。
   */
  auto defaultCollection() -> BangumiResult<QDBusObjectPath> {
    auto reply = call(QString::fromLatin1(kServicePath),
                      QString::fromLatin1(kServiceInterface),
                      QStringLiteral("ReadAlias"), {QStringLiteral("default")});
    if (!reply) {
      return ilias::Err(std::move(reply.error()));
    }
    if (reply->arguments().size() != 1) {
      return ilias::Err(systemCredentialError(
          QStringLiteral("Secret Service 的 ReadAlias 响应格式无效")));
    }
    auto collection = qdbus_cast<QDBusObjectPath>(reply->arguments().front());
    if (!collection.path().isEmpty() &&
        collection.path() != QStringLiteral("/")) {
      return collection;
    }

    QVariantMap properties;
    properties.insert(QStringLiteral("org.freedesktop.Secret.Collection.Label"),
                      QStringLiteral("Default keyring"));
    reply = call(QString::fromLatin1(kServicePath),
                 QString::fromLatin1(kServiceInterface),
                 QStringLiteral("CreateCollection"),
                 {properties, QStringLiteral("default")});
    if (!reply) {
      return ilias::Err(std::move(reply.error()));
    }
    if (reply->arguments().size() != 2) {
      return ilias::Err(systemCredentialError(
          QStringLiteral("Secret Service 的 CreateCollection 响应格式无效")));
    }
    collection = qdbus_cast<QDBusObjectPath>(reply->arguments().at(0));
    auto promptResult =
        completePrompt(qdbus_cast<QDBusObjectPath>(reply->arguments().at(1)));
    if (!promptResult) {
      return ilias::Err(std::move(promptResult.error()));
    }
    if ((collection.path().isEmpty() ||
         collection.path() == QStringLiteral("/")) &&
        promptResult->isValid()) {
      collection = qdbus_cast<QDBusObjectPath>(*promptResult);
    }
    if (collection.path().isEmpty() ||
        collection.path() == QStringLiteral("/")) {
      return ilias::Err(systemCredentialError(
          QStringLiteral("Secret Service 未返回有效的默认 collection")));
    }
    return collection;
  }

  /**
   * @brief 等待一个 Secret Service prompt 完成。
   * @param prompt Prompt 对象路径；`/` 表示无需提示。
   * @pre prompt 来自当前服务的一次方法响应。
   * @post 无提示时返回无效 QVariant；成功提示返回
   * Completed.result；取消或超时返回 CredentialStoreError。
   */
  auto completePrompt(const QDBusObjectPath &prompt)
      -> BangumiResult<QVariant> {
    if (prompt.path().isEmpty() || prompt.path() == QStringLiteral("/")) {
      return QVariant{};
    }

    SecretServicePromptWaiter waiter;
    QEventLoop loop;
    waiter.setEventLoop(&loop);
    if (!mConnection.connect(QString::fromLatin1(kServiceName), prompt.path(),
                             QString::fromLatin1(kPromptInterface),
                             QStringLiteral("Completed"), &waiter,
                             SLOT(completed(bool, QDBusVariant)))) {
      return ilias::Err(systemCredentialError(
          QStringLiteral("无法监听 Secret Service 系统提示")));
    }
    auto disconnect = qScopeGuard([this, &prompt, &waiter]() {
      mConnection.disconnect(QString::fromLatin1(kServiceName), prompt.path(),
                             QString::fromLatin1(kPromptInterface),
                             QStringLiteral("Completed"), &waiter,
                             SLOT(completed(bool, QDBusVariant)));
    });

    auto reply = call(prompt.path(), QString::fromLatin1(kPromptInterface),
                      QStringLiteral("Prompt"), {QString{}});
    if (!reply) {
      return ilias::Err(std::move(reply.error()));
    }
    if (!waiter.isCompleted()) {
      QTimer timer;
      timer.setSingleShot(true);
      QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
      timer.start(kPromptTimeoutMilliseconds);
      loop.exec();
    }
    if (!waiter.isCompleted()) {
      return ilias::Err(
          systemCredentialError(QStringLiteral("等待系统凭据提示超时")));
    }
    if (waiter.isDismissed()) {
      return ilias::Err(
          systemCredentialError(QStringLiteral("系统凭据提示已取消")));
    }
    return waiter.result().variant();
  }

  QDBusConnection mConnection;
};

#elif defined(Q_OS_WIN)

constexpr auto kCredentialTarget = L"anime-land/bangumi/oauth-token";

/**
 * @brief 把 Win32 错误码转换为不含凭据的可读文本。
 * @post 返回的文本只包含系统错误描述与数值错误码。
 */
auto windowsErrorMessage(DWORD code) -> QString {
  wchar_t *buffer = nullptr;
  const auto size = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
  QString message =
      size == 0
          ? QStringLiteral("Win32 error %1").arg(code)
          : QString::fromWCharArray(buffer, static_cast<int>(size)).trimmed();
  if (buffer != nullptr) {
    LocalFree(buffer);
  }
  return message;
}

/**
 * @brief 使用 Windows Credential Manager 的系统凭据 provider。
 * @invariant 条目类型固定为 CRED_TYPE_GENERIC，且不缓存 CredentialBlob。
 */
class WindowsCredentialProvider final : public SystemCredentialProvider {
public:
  /** @copydoc SystemCredentialProvider::load() */
  auto load() -> BangumiResult<std::optional<QByteArray>> override {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(kCredentialTarget, CRED_TYPE_GENERIC, 0, &credential)) {
      const auto error = GetLastError();
      if (error == ERROR_NOT_FOUND) {
        return std::optional<QByteArray>{};
      }
      return ilias::Err(systemCredentialError(
          QStringLiteral("读取 Windows Credential Manager 失败：%1")
              .arg(windowsErrorMessage(error))));
    }
    const std::unique_ptr<CREDENTIALW, decltype(&CredFree)> holder(credential,
                                                                   &CredFree);
    if (credential->CredentialBlobSize == 0 ||
        credential->CredentialBlobSize >
            static_cast<DWORD>(kMaximumCredentialSize)) {
      return ilias::Err(systemCredentialError(
          QStringLiteral("Windows 系统凭据大小无效：%1 bytes")
              .arg(credential->CredentialBlobSize)));
    }
    return std::optional<QByteArray>{
        QByteArray(reinterpret_cast<const char *>(credential->CredentialBlob),
                   static_cast<qsizetype>(credential->CredentialBlobSize))};
  }

  /** @copydoc SystemCredentialProvider::save(const QByteArray &) */
  auto save(const QByteArray &data) -> BangumiResult<void> override {
    if (data.isEmpty() || data.size() > kMaximumCredentialSize ||
        data.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
      return ilias::Err(systemCredentialError(
          QStringLiteral("拒绝保存大小无效的 Windows 系统凭据：%1 bytes")
              .arg(data.size())));
    }
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t *>(kCredentialTarget);
    credential.CredentialBlobSize = static_cast<DWORD>(data.size());
    credential.CredentialBlob =
        reinterpret_cast<LPBYTE>(const_cast<char *>(data.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t *>(L"bangumi");
    if (!CredWriteW(&credential, 0)) {
      const auto error = GetLastError();
      return ilias::Err(systemCredentialError(
          QStringLiteral("保存 Windows Credential Manager 失败：%1")
              .arg(windowsErrorMessage(error))));
    }
    return {};
  }

  /** @copydoc SystemCredentialProvider::clear() */
  auto clear() -> BangumiResult<void> override {
    if (CredDeleteW(kCredentialTarget, CRED_TYPE_GENERIC, 0)) {
      return {};
    }
    const auto error = GetLastError();
    if (error == ERROR_NOT_FOUND) {
      return {};
    }
    return ilias::Err(systemCredentialError(
        QStringLiteral("删除 Windows Credential Manager 凭据失败：%1")
            .arg(windowsErrorMessage(error))));
  }
};

#elif defined(Q_OS_MACOS)

/**
 * @brief 把 Security.framework 状态转换为不含凭据的可读文本。
 * @post 返回系统状态说明与 OSStatus 数值。
 */
auto securityErrorMessage(OSStatus status) -> QString {
  const auto message = SecCopyErrorMessageString(status, nullptr);
  if (message == nullptr) {
    return QStringLiteral("OSStatus %1").arg(status);
  }
  const auto length = CFStringGetLength(message);
  QString result(static_cast<qsizetype>(length), Qt::Uninitialized);
  CFStringGetCharacters(message, CFRangeMake(0, length),
                        reinterpret_cast<UniChar *>(result.data()));
  CFRelease(message);
  return result;
}

/**
 * @brief 使用 macOS Keychain Services 的系统凭据 provider。
 * @invariant 条目由固定 service/account 唯一识别，且不缓存返回的 CFData。
 */
class MacCredentialProvider final : public SystemCredentialProvider {
public:
  /** @copydoc SystemCredentialProvider::load() */
  auto load() -> BangumiResult<std::optional<QByteArray>> override {
    auto query = baseQuery();
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef value = nullptr;
    const auto status = SecItemCopyMatching(query, &value);
    CFRelease(query);
    if (status == errSecItemNotFound) {
      return std::optional<QByteArray>{};
    }
    if (status != errSecSuccess) {
      return ilias::Err(
          systemCredentialError(QStringLiteral("读取 macOS Keychain 失败：%1")
                                    .arg(securityErrorMessage(status))));
    }
    const auto data = static_cast<CFDataRef>(value);
    const auto size = CFDataGetLength(data);
    if (size <= 0 || size > kMaximumCredentialSize) {
      CFRelease(value);
      return ilias::Err(systemCredentialError(
          QStringLiteral("macOS 系统凭据大小无效：%1 bytes").arg(size)));
    }
    QByteArray result(reinterpret_cast<const char *>(CFDataGetBytePtr(data)),
                      static_cast<qsizetype>(size));
    CFRelease(value);
    return std::optional<QByteArray>{std::move(result)};
  }

  /** @copydoc SystemCredentialProvider::save(const QByteArray &) */
  auto save(const QByteArray &data) -> BangumiResult<void> override {
    if (data.isEmpty() || data.size() > kMaximumCredentialSize) {
      return ilias::Err(systemCredentialError(
          QStringLiteral("拒绝保存大小无效的 macOS 系统凭据：%1 bytes")
              .arg(data.size())));
    }
    const auto value = CFDataCreate(
        kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(data.constData()),
        static_cast<CFIndex>(data.size()));
    auto query = baseQuery();
    const void *keys[] = {kSecValueData};
    const void *values[] = {value};
    const auto update = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1,
                                           &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
    auto status = SecItemUpdate(query, update);
    if (status == errSecItemNotFound) {
      CFDictionarySetValue(query, kSecValueData, value);
      CFDictionarySetValue(query, kSecAttrLabel,
                           CFSTR("anime-land Bangumi OAuth token"));
      status = SecItemAdd(query, nullptr);
    }
    CFRelease(update);
    CFRelease(query);
    CFRelease(value);
    if (status != errSecSuccess) {
      return ilias::Err(
          systemCredentialError(QStringLiteral("保存 macOS Keychain 失败：%1")
                                    .arg(securityErrorMessage(status))));
    }
    return {};
  }

  /** @copydoc SystemCredentialProvider::clear() */
  auto clear() -> BangumiResult<void> override {
    auto query = baseQuery();
    const auto status = SecItemDelete(query);
    CFRelease(query);
    if (status == errSecSuccess || status == errSecItemNotFound) {
      return {};
    }
    return ilias::Err(
        systemCredentialError(QStringLiteral("删除 macOS Keychain 凭据失败：%1")
                                  .arg(securityErrorMessage(status))));
  }

private:
  /**
   * @brief 创建由调用方释放的 Keychain 查询字典。
   * @post 返回字典含 generic-password、固定 service 与固定 account 三个条件。
   */
  static auto baseQuery() -> CFMutableDictionaryRef {
    auto query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                           &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, CFSTR("anime-land"));
    CFDictionarySetValue(query, kSecAttrAccount, CFSTR("bangumi/oauth-token"));
    return query;
  }
};

#endif

} // namespace

auto createPlatformSystemCredentialProvider()
    -> BangumiResult<std::unique_ptr<SystemCredentialProvider>> {
#if defined(Q_OS_LINUX)
  return std::make_unique<SecretServiceCredentialProvider>();
#elif defined(Q_OS_WIN)
  return std::make_unique<WindowsCredentialProvider>();
#elif defined(Q_OS_MACOS)
  return std::make_unique<MacCredentialProvider>();
#else
  return ilias::Err(
      bangumiError(BangumiErrorCode::UnsupportedCredentialStore,
                   QStringLiteral("当前平台构建不支持系统凭据库")));
#endif
}

} // namespace anime_land::detail
