/**
 * @file protocol.hpp
 * @brief Bangumi HTTP payload 的强类型协议定义。
 *
 * @details
 * 类型名描述 Bangumi 协议中的角色，而不是所用的传输后端。当前由 RapidJSON
 * backend 编解码；若以后增加 CBOR 等后端，协议类名和业务调用方无需变化。
 * Neko::value 是 NekoProtoTools 的扩展点，rename_tag 只描述官方 wire key。
 *
 * @par 结构校验边界
 * decode() 保证必需字段存在、JSON 类型正确且基础数值可无损放入 C++ 类型。
 * 非空字符串、分页范围、枚举取值等业务条件由协议对象到领域对象的转换阶段
 * 验证，不能把“成功反序列化”理解为“业务数据已经有效”。
 *
 * @par 可选字段语义
 * std::optional 同时接收字段缺失和 JSON null。编码空 optional 时省略字段；
 * 这遵循 NekoProtoTools optional parser 的默认协议。
 */
#pragma once

#include "common/qt_serialization.hpp"

#include <QByteArray>
#include <QString>

#include <nekoproto/serialization/json/rapid_json_serializer.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace anime_land::bangumi_protocol {
using namespace NEKO_NAMESPACE;

/** @brief Bangumi JSON 文本的输出格式，不暴露具体 JSON 后端类型。 */
enum class JsonFormat { Compact,
                        Indented };

/**
 * @brief Bangumi API 的通用错误响应。
 * @pre 输入根节点必须为 object；三个字段允许缺失或为 null。
 * @post 成功解码后，调用方只读取 C++ optional，不再访问 JSON DOM。
 */
struct ApiErrorResponse {
    std::optional<QString> error;
    std::optional<QString> title;
    std::optional<QString> description;

    // clang-format off
    struct Neko {
        static constexpr auto value = Object(
            "error",       &ApiErrorResponse::error,
            "title",       &ApiErrorResponse::title,
            "description", &ApiErrorResponse::description
        );
    };
    // clang-format on
};

/**
 * @brief OAuth access-token endpoint 的成功响应。
 * @pre access_token、refresh_token、token_type、user_id、expires_in 必须存在
 * 且类型正确；scope 允许缺失或 null。
 * @post 仅建立结构化协议值；token 非空、expiresIn > 0 等条件由认证层验证。
 */
struct OAuthTokenResponse {
    QString accessToken;
    QString refreshToken;
    QString tokenType;
    std::optional<QString> scope;
    std::int64_t userId = 0;
    std::int64_t expiresIn = 0;

    // clang-format off
    struct Neko {
        static constexpr auto value = Object(
            "accessToken",  make_tags<rename_tag<"access_token">>(&OAuthTokenResponse::accessToken),
            "refreshToken", make_tags<rename_tag<"refresh_token">>(&OAuthTokenResponse::refreshToken),
            "tokenType",    make_tags<rename_tag<"token_type">>(&OAuthTokenResponse::tokenType),
            "scope",        &OAuthTokenResponse::scope,
            "userId",       make_tags<rename_tag<"user_id">>(&OAuthTokenResponse::userId),
            "expiresIn",    make_tags<rename_tag<"expires_in">>(&OAuthTokenResponse::expiresIn)
        );
    };
    // clang-format on
};

/**
 * @brief 把完整 HTTP body 解码为一个反射协议对象。
 * @tparam T 可由 RapidJSON Reader 和 NekoProtoTools parser 读取的协议类型。
 * @param data 包含单个完整 UTF-8 JSON 根值的响应 body。
 * @param value 仅在反序列化全部成功时提交的新值。
 * @pre data 大小必须已由调用端的响应上限检查；T 必须提供 Neko 元数据。
 * @return 成功时为 nullopt；失败时为不含敏感原文的诊断消息。
 * @post 成功后 value 为强类型协议值，调用方无需再持有或查询 JSON DOM。
 */
template <typename T>
auto decode(const QByteArray &data, T &value) -> std::optional<QString> {
    RapidJsonInputSerializer serializer(data.constData(), static_cast<std::size_t>(data.size()));
    if (serializer(value)) {
        return std::nullopt;
    }
    if (serializer.error() == nullptr) {
        return QStringLiteral("未知 RapidJSON 反序列化错误");
    }
    return QString::fromStdString(serializer.error()->msg);
}

/**
 * @brief 把一个反射协议对象编码成完整 HTTP/file body。
 * @tparam T 可由 RapidJSON Writer 和 NekoProtoTools parser 写出的协议类型。
 * @param value 待编码的协议值。
 * @param format 紧凑或缩进格式。
 * @pre value 中的浮点值必须有限，字符串必须是合法 UTF-8。
 * @return 成功时返回完整 UTF-8 body；任一字段无法编码时返回 nullopt。
 * @post 不修改 value；返回 body 不依赖 serializer 的生命周期。
 */
template <typename T>
auto encode(const T &value, JsonFormat format = JsonFormat::Compact)
    -> std::optional<QByteArray> {
    std::vector<char> data;
    bool serialized = false;
    if (format == JsonFormat::Compact) {
        RapidJsonOutputSerializer serializer(data);
        serialized = serializer(value) && serializer.end();
    }
    else {
        RapidJsonOutputSerializer<NEKO_NAMESPACE::detail::PrettyJsonWriter<>> serializer(data, JsonOutputFormatOptions::Default());
        serialized = serializer(value) && serializer.end();
    }
    if (!serialized || data.size() > static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())) {
        return std::nullopt;
    }
    return QByteArray(data.data(), static_cast<qsizetype>(data.size()));
}

} // namespace anime_land::bangumi_protocol
