/**
 * @file protocol.hpp
 * @brief Bangumi HTTP payload 的强类型协议定义。
 *
 * @details
 * 类型名描述 Bangumi 协议中的角色，而不是所用的传输后端。当前由 QtJson
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

#include "common/qt_json_serializer.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QString>

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace anime_land::bangumi_protocol {

/**
 * @brief Bangumi API 的通用错误响应。
 * @pre 输入根节点必须为 object；三个字段允许缺失或为 null。
 * @post 成功解码后，调用方只读取 C++ optional，不再访问 JSON DOM。
 */
struct ApiErrorResponse {
  std::optional<QString> error;
  std::optional<QString> title;
  std::optional<QString> description;

  struct Neko {
    static constexpr auto value = NEKO_NAMESPACE::Object(
        "error", &ApiErrorResponse::error, "title", &ApiErrorResponse::title,
        "description", &ApiErrorResponse::description);
  };
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

  struct Neko {
    static constexpr auto value = NEKO_NAMESPACE::Object(
        "accessToken",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::rename_tag<"access_token">>(
            &OAuthTokenResponse::accessToken),
        "refreshToken",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::rename_tag<"refresh_token">>(
            &OAuthTokenResponse::refreshToken),
        "tokenType",
        NEKO_NAMESPACE::make_tags<NEKO_NAMESPACE::rename_tag<"token_type">>(
            &OAuthTokenResponse::tokenType),
        "scope", &OAuthTokenResponse::scope, "userId",
        NEKO_NAMESPACE::make_tags<NEKO_NAMESPACE::rename_tag<"user_id">>(
            &OAuthTokenResponse::userId),
        "expiresIn",
        NEKO_NAMESPACE::make_tags<NEKO_NAMESPACE::rename_tag<"expires_in">>(
            &OAuthTokenResponse::expiresIn));
  };
};

/**
 * @brief 把完整 HTTP body 解码为一个反射协议对象。
 * @tparam T 可由 QtJson Reader 和 NekoProtoTools parser 读取的协议类型。
 * @param data 包含单个完整 UTF-8 JSON 根值的响应 body。
 * @param value 仅在反序列化全部成功时提交的新值。
 * @pre data 大小必须已由调用端的响应上限检查；T 必须提供 Neko 元数据。
 * @return 成功时为 nullopt；失败时为不含敏感原文的诊断消息。
 * @post 成功后 value 为强类型协议值，调用方无需再持有或查询 QJsonValue。
 */
template <typename T>
auto decode(const QByteArray &data, T &value) -> std::optional<QString> {
  NEKO_NAMESPACE::QtJsonInputSerializer serializer(data);
  if (serializer(value)) {
    return std::nullopt;
  }
  if (serializer.error() == nullptr) {
    return QStringLiteral("未知 Qt JSON 反序列化错误");
  }
  return QString::fromStdString(serializer.error()->msg);
}

/**
 * @brief 把一个反射协议对象编码成完整 HTTP/file body。
 * @tparam T 可由 QtJson Writer 和 NekoProtoTools parser 写出的协议类型。
 * @param value 待编码的协议值。
 * @param format Qt JSON 的紧凑或缩进格式。
 * @pre value 中的浮点值必须有限，无符号整数不得大于 INT64_MAX。
 * @return 成功时返回完整 UTF-8 body；任一字段无法编码时返回 nullopt。
 * @post 不修改 value；返回 body 不依赖 serializer 的生命周期。
 */
template <typename T>
auto encode(const T &value,
            QJsonDocument::JsonFormat format = QJsonDocument::Compact)
    -> std::optional<QByteArray> {
  QByteArray data;
  NEKO_NAMESPACE::QtJsonOutputSerializer serializer(data, format);
  if (!serializer(value) || !serializer.end()) {
    return std::nullopt;
  }
  return data;
}

} // namespace anime_land::bangumi_protocol
