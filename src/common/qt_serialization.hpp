/**
 * @file qt_serialization.hpp
 * @brief 与具体后端无关的 Qt 基础类型 NekoProtoTools parser。
 *
 * @details
 * Qt native backend 可通过 marker 走 QString 零中间 std::string 路径；其他
 * 文本 backend 统一使用严格 UTF-8，从而保持 TOML/JSON 等后端语义一致。
 */
#pragma once

#include "common/qt_version.hpp"

#include <QByteArray>
#include <QString>
#include <QStringConverter>
#include <QUrl>

#include <nekoproto/serialization/parsing/parsers.hpp>

#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

NEKO_BEGIN_NAMESPACE

namespace qt_serialization_detail {

/**
 * @brief 严格把 UTF-8 字节提交到 QString。
 * @pre utf8 必须是完整、合法的 UTF-8，长度可由 qsizetype 表示。
 * @post 成功时 value 被完整替换；失败时 value 保持不变。
 */
inline auto assignUtf8(std::string_view utf8, QString &value) -> ParserResult {
    if (utf8.size() >
        static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())) {
        return detail::parser_error(sa::ErrorCode::InvalidLength, "UTF-8 string is too large for QString");
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString parsed =
        decoder(QByteArrayView(utf8.data(), static_cast<qsizetype>(utf8.size())));
    if (decoder.hasError()) {
        return detail::parser_error(sa::ErrorCode::ParseError, "Invalid UTF-8 string");
    }
    value = std::move(parsed);
    return sa::success();
}

} // namespace qt_serialization_detail

/**
 * @brief QString 的跨后端 parser。
 *
 * @par 前置条件
 * 非 Qt backend 的字符串通道必须使用 UTF-8；Qt native writer/reader 必须分别
 * 声明 QtNativeString marker 或提供 toQString()。
 *
 * @par 后果
 * Qt JSON 路径直接传递 QString；其他后端严格编解码 UTF-8，非法输入失败且
 * 不修改目标 QString。
 */
template <>
struct CustomParser<QString> {
    /**
   * @brief 写入 QString。
   * @pre writer 满足 Neko Writer 协议。
   * @post native writer 无中间转换；通用 writer 接收 UTF-8 string_view。
   */
    template <typename W, typename Parent, typename Tags>
    static auto write(W &writer, const QString &value, const Parent &parent,
                      const Tags &tags) -> ParserResult {
        if constexpr (requires { typename W::QtNativeString; }) {
            parsing::Parent<W>::addValue(writer, value, parent, tags);
            return sa::success();
        }
        const QByteArray utf8 = value.toUtf8();
        return parser_write<W>(writer, std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())), parent, tags);
    }

    /**
   * @brief 读取 QString。
   * @pre 输入节点必须是后端的 string 类型。
   * @post 成功时替换 value；非法类型或 UTF-8 时 value 不变。
   */
    template <typename R, typename Tags>
    static auto read(typename R::InputValueType input, QString &value,
                     const Tags &tags) -> ParserResult {
        if constexpr (requires { R::toQString(input); }) {
            auto result = R::toQString(input);
            if (!result) {
                return result.error();
            }
            value = std::move(result.value());
            return sa::success();
        }
        else if constexpr (
            requires {
                R::template toStringView<char, std::char_traits<char>>(input, tags);
            } ||
            requires {
                R::template toStringView<char, std::char_traits<char>>(input);
            }) {
            std::string_view utf8;
            auto result = parser_read<R>(input, utf8, tags);
            if (!result) {
                return result;
            }
            return qt_serialization_detail::assignUtf8(utf8, value);
        }
        else {
            std::string utf8;
            auto result = parser_read<R>(input, utf8, tags);
            if (!result) {
                return result;
            }
            return qt_serialization_detail::assignUtf8(utf8, value);
        }
    }

    /** @brief 声明 QString 的后端无关 schema 为 string。 */
    static auto toSchema() -> parsing::schema::Type {
        return parser_schema<std::string>();
    }
};

/**
 * @brief QUrl 的严格字符串协议 parser。
 * @pre 写入值必须有效；读取文本必须能由 QUrl::StrictMode 接受。
 * @post wire 形式使用 FullyEncoded，读取失败不修改目标 QUrl。
 */
template <>
struct CustomParser<QUrl> {
    /**
   * @brief 以 FullyEncoded 字符串写入 URL。
   * @pre value.isValid() 为 true。
   * @post 无效 URL 返回 InvalidType。
   */
    template <typename W, typename Parent, typename Tags>
    static auto write(W &writer, const QUrl &value, const Parent &parent,
                      const Tags &tags) -> ParserResult {
        if (!value.isValid()) {
            return detail::parser_error(sa::ErrorCode::InvalidType, "Cannot serialize an invalid QUrl");
        }
        return parser_write<W>(writer, value.toString(QUrl::FullyEncoded), parent, tags);
    }

    /**
   * @brief 用 StrictMode 读取 URL。
   * @pre 输入节点为 string。
   * @post 成功时替换 value；语法非法时返回 ParseError。
   */
    template <typename R, typename Tags>
    static auto read(typename R::InputValueType input, QUrl &value,
                     const Tags &tags) -> ParserResult {
        QString encoded;
        auto result = parser_read<R>(input, encoded, tags);
        if (!result) {
            return result;
        }

        QUrl parsed(encoded, QUrl::StrictMode);
        if (!parsed.isValid()) {
            const QByteArray reason = parsed.errorString().toUtf8();
            return detail::parser_error(sa::ErrorCode::ParseError, "Invalid QUrl: " + std::string(reason.constData(), static_cast<std::size_t>(reason.size())));
        }
        value = std::move(parsed);
        return sa::success();
    }

    /** @brief 声明 QUrl 的后端无关 schema 为 string。 */
    static auto toSchema() -> parsing::schema::Type {
        return parser_schema<std::string>();
    }
};

NEKO_END_NAMESPACE
