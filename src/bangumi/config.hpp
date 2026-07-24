#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <ilias/result.hpp>
#include <ilias/task.hpp>

#include <nekoproto/serialization/reflection.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace anime_land {
using namespace NEKO_NAMESPACE;
/** Permissions exposed by Bangumi's OAuth application settings page. */
enum class BangumiCapability : std::uint16_t {
    None = 0,
    CollectionRead = 1U << 0U,
    CollectionWrite = 1U << 1U,
    IndexRead = 1U << 2U,
    IndexWrite = 1U << 3U,
    TopicRead = 1U << 4U,
    TopicWrite = 1U << 5U,
    WikiRead = 1U << 6U,
    WikiWrite = 1U << 7U,
};

constexpr auto operator|(BangumiCapability left, BangumiCapability right) noexcept -> BangumiCapability {
    return static_cast<BangumiCapability>(static_cast<std::uint16_t>(left) |
                                          static_cast<std::uint16_t>(right));
}

constexpr auto operator|=(BangumiCapability &left, BangumiCapability right) noexcept -> BangumiCapability & {
    left = left | right;
    return left;
}

constexpr auto hasBangumiCapability(BangumiCapability capabilities, BangumiCapability required) noexcept -> bool {
    return (static_cast<std::uint16_t>(capabilities) &
            static_cast<std::uint16_t>(required)) ==
           static_cast<std::uint16_t>(required);
}

/** Actionable context attached when an API operation lacks a capability. */
struct BangumiCapabilityRemediation {
    BangumiCapability capability = BangumiCapability::None;
    std::string featureId;
    QString featureName;
    QString permissionName;
    QUrl applicationSettingsUrl;
};

/**
 * @brief 已验证的领域值与服务器原始 body。
 * @pre value 必须由对应 endpoint 的完整协议/业务校验产生。
 * @post 业务代码使用 value；rawBody 只供透传、诊断或大数据专用路径使用，
 * 不得重新逐字段解析，且只在Debug模式下有效。
 */
template <typename T>
struct BangumiResponse {
    T value;
    QByteArray rawBody;
};

/** OAuth application identity entered once by the user or application owner. */
struct BangumiOAuthApplication {
    QString clientId;
    std::string clientSecret;
};

/**
 * @brief 已验证并可供请求层使用的 OAuth 凭据。
 * @pre accessToken、refreshToken、tokenType 非空，expiresAt 为绝对 Unix 时间。
 * @post token 字符串只以 QString 在 Qt 网络/持久化边界流动，严禁写入日志。
 */
struct BangumiToken {
    QString accessToken;
    QString refreshToken;
    QString tokenType;
    std::optional<QString> scope;
    std::int64_t userId = 0;
    std::int64_t expiresAt = 0;

    // clang-format off
    struct Neko {
        static constexpr auto value = Object(
            "accessToken",  make_tags<rename_tag<"access_token">>(&BangumiToken::accessToken),
            "refreshToken", make_tags<rename_tag<"refresh_token">>(&BangumiToken::refreshToken),
            "tokenType",    make_tags<rename_tag<"token_type">>(&BangumiToken::tokenType),
            "scope",        &BangumiToken::scope,
            "userId",       make_tags<rename_tag<"user_id">>(&BangumiToken::userId),
            "expiresAt",    make_tags<rename_tag<"expires_at">>(&BangumiToken::expiresAt)
        );
    };
    // clang-format on
};

/**
 * @brief /v0/me 内嵌头像的协议/领域对象。
 * @pre avatar 根必须为 object；small/large 允许缺失或 null。
 * @post optional 精确保留服务器的 missing/null 语义。
 */
struct BangumiUserAvatar {
    std::optional<QString> small;
    std::optional<QString> large;

    // clang-format off
    struct Neko {
        static constexpr auto value = Object(
            "small", &BangumiUserAvatar::small,
            "large", &BangumiUserAvatar::large
        );
    };
    // clang-format on
};

/**
 * @brief 经过 /v0/me 协议和业务校验的最小账号领域对象。
 * @pre id 为正，username 非空；头像字段允许为空。
 * @post 调用方无需保留响应 DOM，也不进行 QString/std::string 往返。
 */
struct BangumiUser {
    std::int64_t id = 0;
    QString username;
    QString nickname;
    BangumiUserAvatar avatar;

    // clang-format off
    struct Neko {
        static constexpr auto value = Object(
            "id",       &BangumiUser::id,
            "username", &BangumiUser::username,
            "nickname", &BangumiUser::nickname,
            "avatar",   &BangumiUser::avatar
        );
    };
    // clang-format on
};

enum class BangumiErrorCode {
    InvalidConfiguration,
    InvalidState,
    CallbackListenFailed,
    BrowserOpenFailed,
    AuthorizationDenied,
    InvalidCallback,
    CallbackTimeout,
    NetworkError,
    InvalidResponse,
    Unauthorized,
    MissingCapability,
    UnsupportedCredentialStore,
    CredentialStoreError,
    Cancelled,
    NotLoggedIn,
    TokenExpired,
};

struct BangumiError {
    BangumiErrorCode code = BangumiErrorCode::InvalidResponse;
    QString message;
    std::optional<BangumiCapabilityRemediation> remediation;
};

template <typename T>
using BangumiResult = ilias::Result<T, BangumiError>;

auto bangumiError(BangumiErrorCode code, QString message) -> BangumiError;
auto bangumiErrorCodeName(BangumiErrorCode code) -> std::string_view;

/**
 * @brief TokenStore 的持久化策略。
 *
 * Memory 仅用于当前进程，File 使用用户明确选择的普通文件，System 使用操作系统
 * 凭据库。System 与 File 之间不存在自动降级关系。
 */
enum class TokenStoreKind {
    Memory,
    File,
    System,
};

/**
 * @brief TokenStore 工厂选项。
 * @invariant kind 不是 File 时 filePath 必须为空。
 * @invariant 默认构造选择 System，因而不会默认把 OAuth token 写入普通文件。
 */
struct TokenStoreOptions {
    TokenStoreKind kind = TokenStoreKind::System;
    QString filePath;
};

/**
 * @brief 把命令行存储名称解析为 TokenStoreKind。
 * @param name 大小写敏感的 ASCII 名称：memory、file 或 system。
 * @pre name 在调用期间保持有效。
 * @post 成功时不修改外部状态；未知名称返回 InvalidConfiguration。
 */
auto parseTokenStoreKind(std::string_view name) -> BangumiResult<TokenStoreKind>;

/**
 * @brief Bangumi OAuth token 的持久化边界。
 *
 * 所有操作均返回 Task，使文件或平台凭据 API 可以离开 Qt 事件循环线程执行。
 * 每个实例只表示一种明确的存储策略，不会静默切换后端。
 *
 * @invariant 同一实例的 load、save 与 clear 不应被并发调用。
 */
class TokenStore {
public:
    /**
     * @brief 释放 TokenStore。
     * @pre 不得仍有引用该实例的未完成操作。
     * @post 实例拥有的进程内敏感数据会尽力清零；持久化条目不被自动删除。
     */
    virtual ~TokenStore() = default;

    /**
     * @brief 读取当前策略保存的 token。
     * @pre 调用方已驱动 ilias/Qt 异步运行时，且没有并发操作同一实例。
     * @post 不存在 token 时返回空 optional；成功读取时返回已验证的 BangumiToken；
     *       无效或不可访问的持久化数据返回 CredentialStoreError。
     */
    virtual auto load() -> ilias::Task<BangumiResult<std::optional<BangumiToken>>> = 0;

    /**
     * @brief 保存或替换 token。
     * @param token 已由认证协议验证的 token。
     * @pre token 的必需字符串非空，数值字段有效，且没有并发操作同一实例。
     * @post 成功时后续 load 返回等价 token；失败时返回对应错误且不得切换后端。
     */
    virtual auto save(const BangumiToken &token) -> ilias::Task<BangumiResult<void>> = 0;

    /**
     * @brief 删除当前策略中的 token。
     * @pre 没有并发操作同一实例。
     * @post 条目不存在也视为成功；成功后 load 返回空 optional。
     */
    virtual auto clear() -> ilias::Task<BangumiResult<void>> = 0;

    /**
     * @brief 根据显式策略创建 TokenStore。
     * @param options 存储策略；默认构造时选择系统凭据库。
     * @pre options 满足 TokenStoreOptions 的不变量。
     * @post 成功时返回唯一拥有的后端；配置或平台不支持时不创建任何凭据条目。
     */
    static auto create(TokenStoreOptions options) -> BangumiResult<std::unique_ptr<TokenStore>>;
};

/** Best-effort clearing for token values kept in process memory. */
void clearBangumiToken(BangumiToken &token) noexcept;

/** Best-effort clearing for an interactively entered application secret. */
void clearBangumiOAuthApplication(BangumiOAuthApplication &application) noexcept;

} // namespace anime_land
