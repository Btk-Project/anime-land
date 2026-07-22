/**
 * @file qt_json_serializer.hpp
 * @brief 基于 Qt JSON DOM 的 NekoProtoTools 序列化后端。
 *
 * @details
 * 本文件实现 NekoProtoTools 的 Reader、Writer、Backend 和 SerializerAdapter
 * 协议。反射 parser 只依赖这些协议，不依赖 QJsonObject 的逐字段调用，因此
 * 业务代码可以在网络边界直接得到强类型 C++ 对象。
 *
 * @par 输入协议
 * 支持 UTF-8 JSON 的 QByteArray、QString、以 NUL 结尾的 const char*、
 * 带显式长度的 const char*，以及已经解析完成的 QJsonDocument/QJsonValue。
 * 文本输入必须是完整的单个 JSON 根值，不允许尾随第二个根值。
 *
 * @par 输出协议
 * 支持 QString、QByteArray、QJsonDocument 和 QJsonValue。QJsonDocument
 * 按 Qt 契约只能保存 object/array 根；标量根应输出到 QString、QByteArray
 * 或 QJsonValue。每个 serializer 实例只允许处理一个根值。
 *
 * @par 数值协议
 * 浮点数必须有限；整数必须无小数部分并落在目标 C++ 类型范围内。由于
 * QJsonValue 的整数表示为 qint64，无符号整数上限为 INT64_MAX。
 */
#pragma once

#include "common/qt_serialization.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>

#include <nekoproto/serialization/parsing/parsers.hpp>
#include <nekoproto/serialization/serializer_adapter.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

NEKO_BEGIN_NAMESPACE

namespace qtjson {

/**
 * @brief 以 QJsonValue 为输入节点的 NekoProtoTools Reader 协议实现。
 *
 * @details InputValueType 按值传递；Qt 的隐式共享使节点复制保持低成本，
 * 同时避免返回指向 QJsonArray/QJsonObject 临时值的悬空指针。
 *
 * @par 前置条件
 * 调用者必须遵循 Neko Reader 协议：只对 array 节点调用数组接口，只对
 * object 节点调用对象接口，并保证 arrayElement 的索引小于 arraySize。
 *
 * @par 后果
 * 所有转换都是事务式的：失败返回 sa::Error，不修改 parser 的目标对象。
 */
struct Reader {
  using InputArrayType = QJsonArray;
  using InputObjectType = QJsonObject;
  using InputValueType = QJsonValue;
  using QtJsonNativeReader = void;

  /**
   * @brief 返回数组元素数量。
   * @pre array 是已经由 toArray() 验证的数组节点。
   * @post 不修改 array。
   */
  static auto arraySize(const InputArrayType &array) noexcept -> std::size_t {
    return static_cast<std::size_t>(array.size());
  }

  /**
   * @brief 按索引取得数组元素。
   * @pre index < arraySize(array)。
   * @post 返回的 QJsonValue 独立于迭代器生命周期，并与原节点隐式共享数据。
   */
  static auto arrayElement(const InputArrayType &array,
                           std::size_t index) noexcept -> InputValueType {
    return array.at(static_cast<qsizetype>(index));
  }

  /**
   * @brief 返回对象成员数量。
   * @pre object 是已经由 toObject() 验证的对象节点。
   * @post 不修改 object。
   */
  static auto objectSize(const InputObjectType &object) noexcept -> std::size_t {
    return static_cast<std::size_t>(object.size());
  }

  /**
   * @brief 用 UTF-8 反射字段名查找对象成员。
   * @param object 已验证的 JSON object。
   * @param name Neko 元数据提供的 UTF-8 字段名。
   * @pre name 必须是合法 UTF-8。
   * @return 字段值；字段不存在时返回 InvalidField，名称非法时返回 ParseError。
   * @post 不修改 object。
   */
  static auto objectField(const InputObjectType &object, std::string_view name)
      -> sa::Result<InputValueType> {
    QString fieldName;
    auto converted = qt_serialization_detail::assignUtf8(name, fieldName);
    if (!converted) {
      return converted.error();
    }
    const auto iterator = object.constFind(fieldName);
    if (iterator == object.constEnd()) {
      return sa::error(sa::ErrorCode::InvalidField,
                       "Field '" + std::string(name) + "' not found");
    }
    return iterator.value();
  }

  /**
   * @brief 依次访问对象成员，供 map parser 使用。
   * @param function 可调用对象，签名兼容 bool(std::string_view, QJsonValue)。
   * @pre function 不得保存字段名 string_view；该视图只在本次回调期间有效。
   * @return 全部成员被访问时为 true；回调返回 false 时立即停止并返回 false。
   * @post 不修改 object。
   */
  template <typename Fn>
  static auto forEachObjectMember(const InputObjectType &object, Fn &&function)
      -> bool {
    for (auto iterator = object.constBegin(); iterator != object.constEnd();
         ++iterator) {
      const QByteArray name = iterator.key().toUtf8();
      if (!function(std::string_view(name.constData(),
                                     static_cast<std::size_t>(name.size())),
                    iterator.value())) {
        return false;
      }
    }
    return true;
  }

  /**
   * @brief 判断 optional/null parser 所需的空节点语义。
   * @return value 为 JSON null 或 Qt undefined 时返回 true。
   * @post 不修改 value。
   */
  static auto isEmpty(const InputValueType &value) noexcept -> bool {
    return value.isNull() || value.isUndefined();
  }

  /**
   * @brief 将任意已定义节点编码成紧凑 UTF-8 JSON。
   * @pre value 不能是 QJsonValue::Undefined。
   * @return 原始 JSON 文本；undefined 返回 InvalidType。
   * @post 不修改 value。
   */
  static auto toRawString(const InputValueType &value)
      -> sa::Result<std::string> {
    if (value.isUndefined()) {
      return sa::error(sa::ErrorCode::InvalidType,
                       "Cannot serialize an undefined QJsonValue");
    }
    const QByteArray json = value.toJson(QJsonDocument::Compact);
    return std::string(json.constData(), static_cast<std::size_t>(json.size()));
  }

  /**
   * @brief 走 Qt 原生路径读取 QString，避免 UTF-8 中间 std::string。
   * @pre value 的 JSON 类型必须为 string。
   * @return 成功时返回共享/移动语义下的 QString；类型不符返回 InvalidType。
   */
  static auto toQString(const InputValueType &value) -> sa::Result<QString> {
    if (!value.isString()) {
      return sa::error(sa::ErrorCode::InvalidType, "Expected string");
    }
    return value.toString();
  }

  /**
   * @brief 将 JSON 标量严格转换为 Neko 基础类型。
   * @tparam T std::string、bool、浮点类型或整数类型。
   * @pre JSON 类型必须与 T 匹配；整数不得含小数，数值必须在 T 范围内。
   * @return 转换值，或描述类型/范围错误的 sa::Error。
   * @post 失败不会产生截断、环绕或非有限浮点值。
   */
  template <typename T>
  static auto toBasicType(const InputValueType &value) -> sa::Result<T> {
    using Value = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<Value, std::string>) {
      if (!value.isString()) {
        return sa::error(sa::ErrorCode::InvalidType, "Expected string");
      }
      const QByteArray utf8 = value.toString().toUtf8();
      return std::string(utf8.constData(),
                         static_cast<std::size_t>(utf8.size()));
    } else if constexpr (std::is_same_v<Value, bool>) {
      if (!value.isBool()) {
        return sa::error(sa::ErrorCode::InvalidType, "Expected bool");
      }
      return value.toBool();
    } else if constexpr (std::is_floating_point_v<Value>) {
      if (!value.isDouble()) {
        return sa::error(sa::ErrorCode::InvalidType, "Expected number");
      }
      const double number = value.toDouble();
      if (!std::isfinite(number) ||
          number < static_cast<double>(std::numeric_limits<Value>::lowest()) ||
          number > static_cast<double>(std::numeric_limits<Value>::max())) {
        return sa::error(sa::ErrorCode::InvalidType,
                         "Floating-point number is out of range");
      }
      return static_cast<Value>(number);
    } else if constexpr (std::is_integral_v<Value> &&
                         std::is_signed_v<Value>) {
      if (!value.isDouble()) {
        return sa::error(sa::ErrorCode::InvalidType, "Expected integer");
      }
      const double number = value.toDouble();
      if (!std::isfinite(number) || std::trunc(number) != number) {
        return sa::error(sa::ErrorCode::InvalidType, "Expected integer");
      }
      if constexpr (sizeof(Value) < sizeof(qint64)) {
        if (number < static_cast<double>(std::numeric_limits<Value>::min()) ||
            number > static_cast<double>(std::numeric_limits<Value>::max())) {
          return sa::error(sa::ErrorCode::InvalidType,
                           "Signed integer is out of range");
        }
      } else {
        constexpr qint64 Sentinel = std::numeric_limits<qint64>::min();
        const qint64 integer = value.toInteger(Sentinel);
        if (integer == Sentinel &&
            number != static_cast<double>(Sentinel)) {
          return sa::error(sa::ErrorCode::InvalidType,
                           "Signed integer is out of range");
        }
        return static_cast<Value>(integer);
      }
      const qint64 integer = value.toInteger();
      if (integer < static_cast<qint64>(std::numeric_limits<Value>::min()) ||
          integer > static_cast<qint64>(std::numeric_limits<Value>::max())) {
        return sa::error(sa::ErrorCode::InvalidType,
                         "Signed integer is out of range");
      }
      return static_cast<Value>(integer);
    } else if constexpr (std::is_integral_v<Value> &&
                         std::is_unsigned_v<Value>) {
      if (!value.isDouble()) {
        return sa::error(sa::ErrorCode::InvalidType,
                         "Expected unsigned integer");
      }
      const double number = value.toDouble();
      const double maximum = static_cast<double>(
          std::min<std::uint64_t>(std::numeric_limits<Value>::max(),
                                  std::numeric_limits<qint64>::max()));
      if (!std::isfinite(number) || std::trunc(number) != number ||
          number < 0 || number > maximum) {
        return sa::error(sa::ErrorCode::InvalidType,
                         "Unsigned integer is out of range");
      }
      return static_cast<Value>(value.toInteger());
    } else {
      static_assert(always_false_v<Value>, "Unsupported Qt JSON basic type");
    }
  }

  /**
   * @brief 验证并取得 JSON array。
   * @pre value 可以是任意已定义 JSON 节点。
   * @return 数组副本；类型不符返回 InvalidType。
   */
  static auto toArray(const InputValueType &value)
      -> sa::Result<InputArrayType> {
    if (!value.isArray()) {
      return sa::error(sa::ErrorCode::InvalidType, "Expected array");
    }
    return value.toArray();
  }

  /**
   * @brief 验证并取得 JSON object。
   * @pre value 可以是任意已定义 JSON 节点。
   * @return 对象副本；类型不符返回 InvalidType。
   */
  static auto toObject(const InputValueType &value)
      -> sa::Result<InputObjectType> {
    if (!value.isObject()) {
      return sa::error(sa::ErrorCode::InvalidType, "Expected object");
    }
    return value.toObject();
  }
};

/**
 * @brief 以 QJsonValue 构建 DOM 的 NekoProtoTools Writer 协议实现。
 *
 * @details
 * Neko reflection parser 会先创建父容器，再递归填充子容器。QJsonArray 和
 * QJsonObject 是 copy-on-write 值，直接保存子对象引用会在 detach 后失效。
 * Writer 因此保存稳定 Node，并在每次子节点变更后沿父链回写，保证根节点
 * 始终是完整的最新文档。
 *
 * @par 前置条件
 * OutputArrayType/OutputObjectType 只能由当前 Writer 返回，并且只在当前
 * Writer 生命周期内使用。调用顺序必须由 Neko Parent/Parser 协议产生。
 *
 * @par 后果
 * Writer 只保留第一次错误。Backend::write() 会在反射 parser 完成后合并
 * 该错误，因此无效 UTF-8、非有限数和超范围无符号整数不会生成成功文档。
 */
class Writer {
public:
  using RawValueType = QJsonValue;
  using QtNativeString = void;
  using QtJsonNativeWriter = void;

private:
  enum class Relation { Root, ArrayElement, ObjectField };

  struct Node {
    QJsonValue value;
    Node *parent = nullptr;
    Relation relation = Relation::Root;
    qsizetype arrayIndex = -1;
    QString fieldName;
  };

public:
  struct OutputArrayType {
    Node *node = nullptr;
  };
  struct OutputObjectType {
    Node *node = nullptr;
  };
  struct OutputValueType {
    Node *node = nullptr;
  };

  /**
   * @brief 创建尚未写入有效根值的 writer。
   * @post 内部根为 JSON null，result() 为成功；第一次 root 写入会重置状态。
   */
  Writer() { reset(QJsonValue()); }

  /**
   * @brief 为 Neko raw_string tag 解析一个完整 JSON 值。
   * @param text UTF-8 编码且恰好包含一个 JSON 根值的文本。
   * @param value 成功时接收解析后的 QJsonValue。
   * @pre text 的长度必须可由 qsizetype 表示。
   * @return 解析成功返回 true；长度、语法或尾随内容非法返回 false。
   * @post 失败时不得使用 value 的内容。
   */
  static auto parseRawValue(std::string_view text, RawValueType &value)
      -> bool {
    if (text.size() >
        static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())) {
      return false;
    }
    QJsonParseError error;
    value = QJsonValue::fromJson(
        QByteArrayView(text.data(), static_cast<qsizetype>(text.size())),
        &error);
    return error.error == QJsonParseError::NoError;
  }

  /**
   * @brief 返回当前完整根节点。
   * @pre 至少已经通过 root/add 协议写入一个根值。
   * @return 引用仅在 Writer 存活且未再次写入时有效。
   */
  auto root() const -> const QJsonValue & { return mNodes.front().value; }

  /**
   * @brief 返回 writer 记录的第一次转换错误。
   * @post 不清除错误；Backend 会把它并入最终 ParserResult。
   */
  auto result() const -> const ParserResult & { return mResult; }

  /**
   * @brief 建立 array 根节点。
   * @pre 当前 serializer 尚未写过第二个根值；size 只作容量提示。
   * @post 旧 DOM 和旧句柄全部失效，返回当前 array 根句柄。
   */
  auto arrayAsRoot(std::size_t /*size*/) -> OutputArrayType {
    reset(QJsonArray{});
    return {&mNodes.front()};
  }

  /**
   * @brief 建立 object 根节点。
   * @pre 当前 serializer 尚未写过第二个根值；size 只作容量提示。
   * @post 旧 DOM 和旧句柄全部失效，返回当前 object 根句柄。
   */
  auto objectAsRoot(std::size_t /*size*/) -> OutputObjectType {
    reset(QJsonObject{});
    return {&mNodes.front()};
  }

  /**
   * @brief 建立 JSON null 根节点。
   * @pre 当前 serializer 尚未写过第二个根值。
   * @post 旧 DOM 和旧句柄全部失效。
   */
  auto nullAsRoot() -> OutputValueType {
    reset(QJsonValue());
    return {&mNodes.front()};
  }

  /**
   * @brief 建立基础类型或 Qt 原生 JSON 类型的根节点。
   * @pre value 必须满足 fromBasicType() 的可表示条件。
   * @post 成功时根节点等价于 value；失败由 result() 暴露。
   */
  template <typename T> auto valueAsRoot(const T &value) -> OutputValueType {
    reset(QJsonValue());
    mNodes.front().value = fromBasicType(value);
    return {&mNodes.front()};
  }

  /**
   * @brief 向 array 父节点追加空 array 子节点。
   * @pre parent 来自当前 Writer 且指向 array。
   * @post 子节点已出现在根 DOM 中，后续子写入会自动向根传播。
   */
  auto addArrayToArray(std::size_t /*size*/, OutputArrayType *parent)
      -> OutputArrayType {
    return {addArrayChild(parent->node, QJsonArray{})};
  }

  /**
   * @brief 在 object 字段中建立空 array 子节点。
   * @pre parent 有效且 name 是合法 UTF-8。
   * @post 成功时字段已插入；失败记录错误供 Backend 返回。
   */
  auto addArrayToObject(std::string_view name, std::size_t /*size*/,
                        OutputObjectType *parent) -> OutputArrayType {
    return {addObjectChild(parent->node, name, QJsonArray{})};
  }

  /**
   * @brief 向 array 父节点追加空 object 子节点。
   * @pre parent 来自当前 Writer 且指向 array。
   * @post 子节点已出现在根 DOM 中，后续子写入会自动向根传播。
   */
  auto addObjectToArray(std::size_t /*size*/, OutputArrayType *parent)
      -> OutputObjectType {
    return {addArrayChild(parent->node, QJsonObject{})};
  }

  /**
   * @brief 在 object 字段中建立空 object 子节点。
   * @pre parent 有效且 name 是合法 UTF-8。
   * @post 成功时字段已插入；失败记录错误供 Backend 返回。
   */
  auto addObjectToObject(std::string_view name, std::size_t /*size*/,
                         OutputObjectType *parent) -> OutputObjectType {
    return {addObjectChild(parent->node, name, QJsonObject{})};
  }

  /**
   * @brief 向 array 追加基础值或 Qt 原生 JSON 值。
   * @pre parent 有效，value 满足 JSON 可表示条件。
   * @post 父节点及所有祖先同步更新；失败只记录第一次错误。
   */
  template <typename T>
  auto addValueToArray(const T &value, OutputArrayType *parent)
      -> OutputValueType {
    QJsonArray array = parent->node->value.toArray();
    array.append(fromBasicType(value));
    parent->node->value = std::move(array);
    propagate(parent->node);
    return {parent->node};
  }

  /**
   * @brief 写入 object 字段值。
   * @pre parent 有效、name 为合法 UTF-8、value 可表示为 JSON。
   * @post 字段覆盖/插入后向根传播；失败只记录第一次错误。
   */
  template <typename T>
  auto addValueToObject(std::string_view name, const T &value,
                        OutputObjectType *parent) -> OutputValueType {
    QString fieldName;
    if (!convertName(name, fieldName)) {
      return {parent->node};
    }
    QJsonObject object = parent->node->value.toObject();
    object.insert(fieldName, fromBasicType(value));
    parent->node->value = std::move(object);
    propagate(parent->node);
    return {parent->node};
  }

  /**
   * @brief 向 array 追加 JSON null。
   * @pre parent 有效且指向 array。
   * @post 父节点及祖先同步更新。
   */
  auto addNullToArray(OutputArrayType *parent) -> OutputValueType {
    return addValueToArray(QJsonValue(), parent);
  }

  /**
   * @brief 写入值为 JSON null 的 object 字段。
   * @pre parent 有效且 name 为合法 UTF-8。
   * @post 字段及祖先同步更新。
   */
  auto addNullToObject(std::string_view name, OutputObjectType *parent)
      -> OutputValueType {
    return addValueToObject(name, QJsonValue(), parent);
  }

  /**
   * @brief 满足可选的 Neko Writer 容器结束协议。
   * @post 无操作；本实现每次变更已经实时提交。
   */
  void endArray(OutputArrayType * /*unused*/) noexcept {}

  /**
   * @brief 满足可选的 Neko Writer 容器结束协议。
   * @post 无操作；本实现每次变更已经实时提交。
   */
  void endObject(OutputObjectType * /*unused*/) noexcept {}

private:
  /** @brief 重建根节点并使旧句柄失效。 @post 清除旧错误。 */
  void reset(QJsonValue value) {
    mNodes.clear();
    mNodes.push_back(Node{.value = std::move(value), .fieldName = {}});
    mResult = sa::success();
  }

  /** @brief 记录第一次错误。 @post 后续错误不会覆盖最初诊断。 */
  void fail(sa::ErrorCode code, std::string message) {
    if (mResult) {
      mResult = sa::error(code, std::move(message));
    }
  }

  /**
   * @brief 严格把 UTF-8 字段名转换为 QString。
   * @post 失败时 result 保存错误，result 参数不作为字段名使用。
   */
  auto convertName(std::string_view name, QString &result) -> bool {
    auto converted = qt_serialization_detail::assignUtf8(name, result);
    if (!converted) {
      mResult = converted.error();
      return false;
    }
    return true;
  }

  /**
   * @brief 建立 array 子节点及稳定句柄。
   * @pre parent 非空且指向 array Node。
   * @post 子节点已经插入并传播到根。
   */
  auto addArrayChild(Node *parent, QJsonValue value) -> Node * {
    QJsonArray array = parent->value.toArray();
    const qsizetype index = array.size();
    array.append(value);
    parent->value = std::move(array);
    propagate(parent);
    mNodes.push_back(Node{.value = std::move(value),
                          .parent = parent,
                          .relation = Relation::ArrayElement,
                          .arrayIndex = index,
                          .fieldName = {}});
    return &mNodes.back();
  }

  /**
   * @brief 建立 object 字段子节点及稳定句柄。
   * @pre parent 非空且指向 object Node；name 为合法 UTF-8。
   * @post 成功时子节点已经插入并传播到根。
   */
  auto addObjectChild(Node *parent, std::string_view name, QJsonValue value)
      -> Node * {
    QString fieldName;
    if (!convertName(name, fieldName)) {
      return parent;
    }
    QJsonObject object = parent->value.toObject();
    object.insert(fieldName, value);
    parent->value = std::move(object);
    propagate(parent);
    mNodes.push_back(Node{.value = std::move(value),
                          .parent = parent,
                          .relation = Relation::ObjectField,
                          .fieldName = std::move(fieldName)});
    return &mNodes.back();
  }

  /**
   * @brief 将一个已变更节点递归回写到根节点。
   * @pre node 属于当前 Writer，父链无环且关系索引/字段名有效。
   * @post root() 反映 node 的最新值。
   */
  void propagate(Node *node) {
    if (node->parent == nullptr) {
      return;
    }
    Node *parent = node->parent;
    if (node->relation == Relation::ArrayElement) {
      QJsonArray array = parent->value.toArray();
      array.replace(node->arrayIndex, node->value);
      parent->value = std::move(array);
    } else {
      QJsonObject object = parent->value.toObject();
      object.insert(node->fieldName, node->value);
      parent->value = std::move(object);
    }
    propagate(parent);
  }

  /**
   * @brief 严格转换 Neko 基础值和 Qt 原生值。
   * @pre std::string 必须是合法 UTF-8；浮点数有限；unsigned <= INT64_MAX。
   * @post 失败返回 null 占位并由 result() 报告，调用方不得把占位当作成功。
   */
  template <typename T> auto fromBasicType(const T &value) -> QJsonValue {
    using Value = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<Value, QJsonValue>) {
      if (value.isUndefined()) {
        fail(sa::ErrorCode::InvalidType,
             "Cannot serialize an undefined QJsonValue");
        return QJsonValue();
      }
      return value;
    } else if constexpr (std::is_same_v<Value, QJsonArray>) {
      return QJsonValue(value);
    } else if constexpr (std::is_same_v<Value, QJsonObject>) {
      return QJsonValue(value);
    } else if constexpr (std::is_same_v<Value, QString>) {
      return QJsonValue(value);
    } else if constexpr (std::is_same_v<Value, std::string> ||
                         std::is_same_v<Value, std::string_view>) {
      QString string;
      auto converted = qt_serialization_detail::assignUtf8(value, string);
      if (!converted) {
        mResult = converted.error();
        return QJsonValue();
      }
      return QJsonValue(string);
    } else if constexpr (std::is_same_v<Value, bool>) {
      return QJsonValue(value);
    } else if constexpr (std::is_floating_point_v<Value>) {
      if (!std::isfinite(value)) {
        fail(sa::ErrorCode::InvalidType,
             "JSON cannot represent a non-finite number");
        return QJsonValue();
      }
      return QJsonValue(static_cast<double>(value));
    } else if constexpr (std::is_enum_v<Value>) {
      return fromBasicType(static_cast<std::underlying_type_t<Value>>(value));
    } else if constexpr (std::is_integral_v<Value> &&
                         std::is_signed_v<Value>) {
      return QJsonValue(static_cast<qint64>(value));
    } else if constexpr (std::is_integral_v<Value> &&
                         std::is_unsigned_v<Value>) {
      if (value > static_cast<std::make_unsigned_t<qint64>>(
                      std::numeric_limits<qint64>::max())) {
        fail(sa::ErrorCode::InvalidType,
             "QJsonValue cannot represent an unsigned integer above INT64_MAX");
        return QJsonValue();
      }
      return QJsonValue(static_cast<qint64>(value));
    } else {
      static_assert(always_false_v<Value>, "Unsupported Qt JSON basic type");
    }
  }

  std::deque<Node> mNodes;
  ParserResult mResult;
};

} // namespace qtjson

namespace qt_json_detail {

/**
 * @brief 解析一个完整 UTF-8 JSON 根值。
 * @pre bytes 的存储在调用期间有效，且长度可由 qsizetype 表示。
 * @post 成功时 value 接收根节点；失败时返回包含 byte offset 的 ParseError。
 */
inline auto parse(QByteArrayView bytes, QJsonValue &value) -> ParserResult {
  QJsonParseError error;
  value = QJsonValue::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError) {
    return sa::error(sa::ErrorCode::ParseError,
                     "Qt JSON parse error at offset " +
                         std::to_string(error.offset) + ": " +
                         error.errorString().toStdString());
  }
  return sa::success();
}

/**
 * @brief 把 QJsonDocument 的 object/array 根提升为 QJsonValue。
 * @pre document 必须包含 object 或 array；Qt 空 document 不被接受。
 * @post 成功时 value 与 document 隐式共享不可变数据。
 */
inline auto valueFromDocument(const QJsonDocument &document,
                              QJsonValue &value) -> ParserResult {
  if (document.isObject()) {
    value = document.object();
    return sa::success();
  }
  if (document.isArray()) {
    value = document.array();
    return sa::success();
  }
  return sa::error(sa::ErrorCode::InvalidType,
                   "QJsonDocument does not contain an object or array root");
}

template <typename>
inline constexpr bool SupportedOutput = false;
template <>
inline constexpr bool SupportedOutput<QString> = true;
template <>
inline constexpr bool SupportedOutput<QByteArray> = true;
template <>
inline constexpr bool SupportedOutput<QJsonDocument> = true;
template <>
inline constexpr bool SupportedOutput<QJsonValue> = true;

} // namespace qt_json_detail

/**
 * @brief 连接 Qt JSON Reader/Writer 与 Neko SerializerAdapter 的后端协议。
 *
 * @details
 * Backend::write/read 只构建或读取内存 DOM；finish() 是唯一把 DOM 提交到
 * 输出 sink 的位置。InputState 在构造期间完成语法解析，之后业务 parser 不再
 * 接触文本或 QJsonDocument。
 *
 * @par 单根条件
 * OutputSerializerAdapter/InputSerializerAdapter 保证每个实例最多处理一个根。
 * 再次调用同一 serializer 会返回 InvalidLength。
 *
 * @par 提交后果
 * QString/QByteArray/QJsonValue 输出被赋值覆盖，不是追加。QJsonDocument
 * 输出遇到标量根会在 finish() 返回 InvalidType，原 sink 不被标量替换。
 */
struct QtJsonBackend {
  using Reader = qtjson::Reader;
  using Writer = qtjson::Writer;
  using JsonValue = QJsonValue;
  using DefaultOutputBuffer = QString;
  using DefaultInputSource = QByteArray;

  /**
   * @brief 单次输出事务的状态。
   * @tparam BufferT QString、QByteArray、QJsonDocument 或 QJsonValue。
   * @pre output 引用对象必须比 serializer 存活更久。
   * @post 成功 finish 后 output 被完整覆盖，flushed 为 true。
   */
  template <typename BufferT> class OutputState {
  public:
    static_assert(qt_json_detail::SupportedOutput<BufferT>,
                  "Unsupported Qt JSON output type");

    /**
     * @brief 使用 Compact 格式建立输出事务。
     * @pre buffer 生命周期覆盖整个 serializer 生命周期。
     * @post 尚未修改 buffer。
     */
    explicit OutputState(BufferT &buffer) noexcept : output(buffer) {}

    /**
     * @brief 使用指定 Qt JSON 格式建立输出事务。
     * @pre buffer 生命周期覆盖整个 serializer 生命周期。
     * @post 尚未修改 buffer；format 仅影响文本 sink。
     */
    OutputState(BufferT &buffer, QJsonDocument::JsonFormat outputFormat) noexcept
        : output(buffer), format(outputFormat) {}

    BufferT &output;
    qtjson::Writer writer;
    QJsonDocument::JsonFormat format = QJsonDocument::Compact;
    bool hasRoot = false;
    bool flushed = false;
  };

  /**
   * @brief 单次输入事务的已解析状态。
   * @tparam SourceT 仅标识 adapter 的构造来源；DOM 始终由 root 自己持有。
   * @post 构造完成后不依赖原文本输入的生命周期。
   */
  template <typename SourceT> class InputState {
  public:
    /**
     * @brief 从 UTF-8 QByteArray 解析根节点。
     * @pre input 必须包含一个完整 JSON 根值。
     * @post 语法结果保存在 result；root 不引用 input 内存。
     */
    explicit InputState(const QByteArray &input) noexcept {
      result = qt_json_detail::parse(QByteArrayView(input), root);
    }

    /**
     * @brief 从 QString JSON 文本解析根节点。
     * @pre input 必须包含一个完整 JSON 根值。
     * @post 先编码 UTF-8 再解析；root 不引用临时 QByteArray。
     */
    explicit InputState(const QString &input) noexcept {
      const QByteArray utf8 = input.toUtf8();
      result = qt_json_detail::parse(QByteArrayView(utf8), root);
    }

    /**
     * @brief 从 NUL 结尾的 UTF-8 C 字符串解析根节点。
     * @pre input 非空且在首个 NUL 前包含完整 JSON；嵌入 NUL 应使用长度重载。
     * @post null 指针或语法错误写入 result，不抛异常。
     */
    explicit InputState(const char *input) noexcept {
      if (input == nullptr) {
        result = sa::error(sa::ErrorCode::ParseError,
                           "Qt JSON input pointer is null");
        return;
      }
      result = qt_json_detail::parse(
          QByteArrayView(input, static_cast<qsizetype>(std::strlen(input))),
          root);
    }

    /**
     * @brief 从显式长度的 UTF-8 字节区间解析根节点。
     * @pre input 非空；[input, input + size) 在构造期间可读。
     * @post 支持嵌入 NUL；超出 qsizetype 或语法非法写入 result。
     */
    InputState(const char *input, std::size_t size) noexcept {
      if (input == nullptr) {
        result = sa::error(sa::ErrorCode::ParseError,
                           "Qt JSON input pointer is null");
        return;
      }
      if (size >
          static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())) {
        result = sa::error(sa::ErrorCode::InvalidLength,
                           "Qt JSON input is too large");
        return;
      }
      result = qt_json_detail::parse(
          QByteArrayView(input, static_cast<qsizetype>(size)), root);
    }

    /**
     * @brief 从已经解析的 QJsonDocument 建立输入事务。
     * @pre input 必须包含 object 或 array 根。
     * @post root 按 Qt 隐式共享规则持有数据，不依赖 input 生命周期。
     */
    explicit InputState(const QJsonDocument &input) noexcept {
      result = qt_json_detail::valueFromDocument(input, root);
    }

    /**
     * @brief 从 QJsonValue 建立输入事务，包括标量根。
     * @pre input 不能是 Undefined。
     * @post root 按值持有输入；Undefined 转为 InvalidType。
     */
    explicit InputState(const QJsonValue &input) noexcept : root(input) {
      if (input.isUndefined()) {
        result = sa::error(sa::ErrorCode::InvalidType,
                           "Qt JSON input is undefined");
      }
    }

    QJsonValue root;
    ParserResult result;
  };

  /**
   * @brief 用 Neko parser 把一个 C++ 根值写入 Qt DOM。
   * @pre state 尚未提交第二个根；T 对 qtjson::Writer 可写。
   * @post 成功时 hasRoot 为 true；sink 仍在 finish() 前保持不变。
   */
  template <typename BufferT, typename T>
  static auto write(OutputState<BufferT> &state, const T &value)
      -> ParserResult {
    auto result = parser_write<qtjson::Writer>(
        state.writer, value, parsing::Parent<qtjson::Writer>::Root{});
    if (result && !state.writer.result()) {
      result = state.writer.result();
    }
    state.hasRoot = static_cast<bool>(result);
    state.flushed = false;
    return result;
  }

  /**
   * @brief 原子地把已完成 DOM 提交到输出 sink。
   * @pre write() 已成功，且 sink 生命周期仍有效。
   * @post 成功时 sink 被完整覆盖且 flushed=true；重复调用保持幂等。
   */
  template <typename BufferT>
  static auto finish(OutputState<BufferT> &state, ParserResult result)
      -> ParserResult {
    if (!result || !state.hasRoot || state.flushed) {
      return result;
    }
    const QJsonValue &root = state.writer.root();
    if constexpr (std::is_same_v<BufferT, QJsonDocument>) {
      if (root.isObject()) {
        state.output = QJsonDocument(root.toObject());
      } else if (root.isArray()) {
        state.output = QJsonDocument(root.toArray());
      } else {
        return sa::error(
            sa::ErrorCode::InvalidType,
            "QJsonDocument output requires an object or array root");
      }
    } else if constexpr (std::is_same_v<BufferT, QJsonValue>) {
      state.output = root;
    } else {
      const QByteArray bytes = root.toJson(state.format);
      if constexpr (std::is_same_v<BufferT, QByteArray>) {
        state.output = bytes;
      } else {
        state.output = QString::fromUtf8(bytes);
      }
    }
    state.flushed = true;
    return result;
  }

  /**
   * @brief 向 OutputSerializerAdapter 报告是否已有有效根值。
   * @post 不提交 sink，也不修改 state。
   */
  template <typename BufferT>
  static auto outputReady(const OutputState<BufferT> &state,
                          const ParserResult &result) noexcept -> bool {
    return state.hasRoot && static_cast<bool>(result);
  }

  /**
   * @brief 返回 InputState 构造期的解析结果。
   * @post 不消费 root，后续成功时仍可执行一次 read()。
   */
  template <typename SourceT>
  static auto inputResult(const InputState<SourceT> &state) -> ParserResult {
    return state.result;
  }

  /**
   * @brief 用 Neko parser 把根 DOM 事务式映射为 C++ 对象。
   * @pre inputResult(state) 成功，T 对 qtjson::Reader 可读。
   * @post 成功时 value 完整更新；反射 parser 失败时不提交部分字段。
   */
  template <typename SourceT, typename T>
  static auto read(InputState<SourceT> &state, T &value) -> ParserResult {
    return parser_read<qtjson::Reader>(state.root, value);
  }
};

/**
 * @brief Qt JSON 单根输出 serializer。
 * @tparam BufferT 默认 QString；也支持 QByteArray/QJsonDocument/QJsonValue。
 * @pre sink 生命周期覆盖 serializer；operator() 最多调用一次。
 * @post end() 成功后 sink 包含完整根值；析构也会调用 end()。
 */
template <typename BufferT = QtJsonBackend::DefaultOutputBuffer>
class QtJsonOutputSerializer
    : public detail::OutputSerializerAdapter<QtJsonBackend, BufferT> {
public:
  using Base = detail::OutputSerializerAdapter<QtJsonBackend, BufferT>;
  using Base::Base;
};

template <typename BufferT>
QtJsonOutputSerializer(BufferT &) -> QtJsonOutputSerializer<BufferT>;
template <typename BufferT>
QtJsonOutputSerializer(BufferT &, QJsonDocument::JsonFormat)
    -> QtJsonOutputSerializer<BufferT>;

/**
 * @brief Qt JSON 单根输入 serializer。
 * @tparam SourceT 由构造参数 deduction guide 推导。
 * @pre 输入满足对应 InputState 构造函数的协议；operator() 最多调用一次。
 * @post 成功后目标是强类型 C++ 值，不依赖输入文本生命周期。
 */
template <typename SourceT = QtJsonBackend::DefaultInputSource>
class QtJsonInputSerializer
    : public detail::InputSerializerAdapter<QtJsonBackend, SourceT> {
public:
  using Base = detail::InputSerializerAdapter<QtJsonBackend, SourceT>;
  using Base::Base;
};

QtJsonInputSerializer(const QByteArray &) -> QtJsonInputSerializer<QByteArray>;
QtJsonInputSerializer(const QString &) -> QtJsonInputSerializer<QString>;
QtJsonInputSerializer(const char *) -> QtJsonInputSerializer<const char *>;
QtJsonInputSerializer(const char *, std::size_t)
    -> QtJsonInputSerializer<const char *>;
QtJsonInputSerializer(const QJsonDocument &)
    -> QtJsonInputSerializer<QJsonDocument>;
QtJsonInputSerializer(const QJsonValue &) -> QtJsonInputSerializer<QJsonValue>;

using QtJsonByteOutputSerializer = QtJsonOutputSerializer<QByteArray>;
using QtJsonDocumentOutputSerializer = QtJsonOutputSerializer<QJsonDocument>;

/**
 * @brief 与 Neko 其他 backend 一致的默认类型集合。
 * @post 调用方可用统一的 Serializer::InputSerializer/OutputSerializer 别名。
 */
struct QtJsonSerializer {
  using OutputSerializer = QtJsonOutputSerializer<>;
  using ByteOutputSerializer = QtJsonByteOutputSerializer;
  using DocumentOutputSerializer = QtJsonDocumentOutputSerializer;
  using InputSerializer = QtJsonInputSerializer<>;
  using JsonValue = QJsonValue;
  using Reader = qtjson::Reader;
  using Writer = qtjson::Writer;
};

/**
 * @brief QJsonValue 在 Qt backend 中的一等公民 parser。
 * @pre 仅对声明 QtJsonNativeReader/Writer marker 的后端参与重载。
 * @post 值直接进入/离开 DOM，不经过文本往返。
 */
template <> struct CustomParser<QJsonValue> {
  /**
   * @brief 原生写入 QJsonValue。
   * @pre value 不能为 Undefined。
   * @post writer 失败状态由 Backend 在 parser 返回后统一合并。
   */
  template <typename W, typename Parent, typename Tags>
  static auto write(W &writer, const QJsonValue &value, const Parent &parent,
                    const Tags &tags) -> ParserResult
    requires requires { typename W::QtJsonNativeWriter; }
  {
    parsing::Parent<W>::addValue(writer, value, parent, tags);
    return sa::success();
  }

  /**
   * @brief 原生读取 QJsonValue。
   * @pre input 是 Reader 提供的已定义节点。
   * @post value 按 Qt 隐式共享规则持有节点。
   */
  template <typename R, typename Tags>
  static auto read(typename R::InputValueType input, QJsonValue &value,
                   const Tags & /*tags*/) -> ParserResult
    requires requires { typename R::QtJsonNativeReader; }
  {
    value = std::move(input);
    return sa::success();
  }
};

/**
 * @brief QJsonObject 在 Qt backend 中的一等公民 parser。
 * @pre 输入节点必须为 object；仅匹配 Qt native marker。
 * @post 不逐字段重建对象，直接使用 Qt 隐式共享值。
 */
template <> struct CustomParser<QJsonObject> {
  /** @brief 原生写入 object。 @post 不进行文本编码。 */
  template <typename W, typename Parent, typename Tags>
  static auto write(W &writer, const QJsonObject &value, const Parent &parent,
                    const Tags &tags) -> ParserResult
    requires requires { typename W::QtJsonNativeWriter; }
  {
    parsing::Parent<W>::addValue(writer, value, parent, tags);
    return sa::success();
  }

  /**
   * @brief 验证并读取 object。
   * @pre input 可以是任意 JSON 节点。
   * @post 类型不符返回 InvalidType 且不修改 value。
   */
  template <typename R, typename Tags>
  static auto read(typename R::InputValueType input, QJsonObject &value,
                   const Tags & /*tags*/) -> ParserResult
    requires requires { typename R::QtJsonNativeReader; }
  {
    if (!input.isObject()) {
      return detail::parser_error(sa::ErrorCode::InvalidType,
                                  "Expected QJsonObject");
    }
    value = input.toObject();
    return sa::success();
  }
};

/**
 * @brief QJsonArray 在 Qt backend 中的一等公民 parser。
 * @pre 输入节点必须为 array；仅匹配 Qt native marker。
 * @post 不逐元素重建数组，直接使用 Qt 隐式共享值。
 */
template <> struct CustomParser<QJsonArray> {
  /** @brief 原生写入 array。 @post 不进行文本编码。 */
  template <typename W, typename Parent, typename Tags>
  static auto write(W &writer, const QJsonArray &value, const Parent &parent,
                    const Tags &tags) -> ParserResult
    requires requires { typename W::QtJsonNativeWriter; }
  {
    parsing::Parent<W>::addValue(writer, value, parent, tags);
    return sa::success();
  }

  /**
   * @brief 验证并读取 array。
   * @pre input 可以是任意 JSON 节点。
   * @post 类型不符返回 InvalidType 且不修改 value。
   */
  template <typename R, typename Tags>
  static auto read(typename R::InputValueType input, QJsonArray &value,
                   const Tags & /*tags*/) -> ParserResult
    requires requires { typename R::QtJsonNativeReader; }
  {
    if (!input.isArray()) {
      return detail::parser_error(sa::ErrorCode::InvalidType,
                                  "Expected QJsonArray");
    }
    value = input.toArray();
    return sa::success();
  }
};

/**
 * @brief QJsonDocument 在 Qt backend 中的一等公民 parser。
 * @pre document 必须包含 object 或 array；Qt 不允许标量 document 根。
 * @post object/array 直接进入/离开 DOM，不进行 JSON 文本往返。
 */
template <> struct CustomParser<QJsonDocument> {
  /**
   * @brief 写入 document 的 object/array 根。
   * @pre value 不是空 QJsonDocument。
   * @post 空 document 返回 InvalidType。
   */
  template <typename W, typename Parent, typename Tags>
  static auto write(W &writer, const QJsonDocument &value,
                    const Parent &parent, const Tags &tags) -> ParserResult
    requires requires { typename W::QtJsonNativeWriter; }
  {
    if (value.isObject()) {
      parsing::Parent<W>::addValue(writer, value.object(), parent, tags);
      return sa::success();
    }
    if (value.isArray()) {
      parsing::Parent<W>::addValue(writer, value.array(), parent, tags);
      return sa::success();
    }
    return detail::parser_error(
        sa::ErrorCode::InvalidType,
        "Cannot serialize an empty QJsonDocument");
  }

  /**
   * @brief 从 object/array 节点建立 document。
   * @pre input 必须是 object 或 array，不能是标量。
   * @post 类型不符返回 InvalidType 且不修改 value。
   */
  template <typename R, typename Tags>
  static auto read(typename R::InputValueType input, QJsonDocument &value,
                   const Tags & /*tags*/) -> ParserResult
    requires requires { typename R::QtJsonNativeReader; }
  {
    if (input.isObject()) {
      value = QJsonDocument(input.toObject());
      return sa::success();
    }
    if (input.isArray()) {
      value = QJsonDocument(input.toArray());
      return sa::success();
    }
    return detail::parser_error(
        sa::ErrorCode::InvalidType,
        "QJsonDocument requires an object or array root");
  }
};

NEKO_END_NAMESPACE
