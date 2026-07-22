#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

#include "bangumi/capability.hpp"
#include "bangumi/config.hpp"
#include "common/app_settings.hpp"

#include <nekoproto/serialization/parsing/parsers.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace anime_land {

enum class BangumiSubjectType : int {
  Book = 1,
  Anime = 2,
  Music = 3,
  Game = 4,
  Real = 6,
};

enum class BangumiCollectionType : int {
  Wish = 1,
  Done = 2,
  Doing = 3,
  OnHold = 4,
  Dropped = 5,
};

/**
 * @brief 判断值是否属于 Bangumi subject type 协议的有效枚举集合。
 * @pre type 可以包含经 static_cast 得到的未知值。
 * @post 不修改 type；仅 1、2、3、4、6 返回 true。
 */
constexpr auto isValidBangumiSubjectType(BangumiSubjectType type) noexcept
    -> bool {
  switch (type) {
  case BangumiSubjectType::Book:
  case BangumiSubjectType::Anime:
  case BangumiSubjectType::Music:
  case BangumiSubjectType::Game:
  case BangumiSubjectType::Real:
    return true;
  }
  return false;
}

/**
 * @brief 判断值是否属于 Bangumi collection type 协议的有效枚举集合。
 * @pre type 可以包含经 static_cast 得到的未知值。
 * @post 不修改 type；仅 1..5 返回 true。
 */
constexpr auto isValidBangumiCollectionType(BangumiCollectionType type) noexcept
    -> bool {
  switch (type) {
  case BangumiCollectionType::Wish:
  case BangumiCollectionType::Done:
  case BangumiCollectionType::Doing:
  case BangumiCollectionType::OnHold:
  case BangumiCollectionType::Dropped:
    return true;
  }
  return false;
}

struct BangumiCollectionQuery {
  std::optional<BangumiSubjectType> subjectType;
  std::optional<BangumiCollectionType> collectionType;
  int limit = 30;
  int offset = 0;
};

/**
 * @brief SlimSubject 的图片协议/领域值。
 * @pre 反序列化时五个字段均必须存在且为 string。
 * @post 成功后该对象可直接交给业务层，无额外协议 DTO。
 */
struct BangumiSubjectImages {
  QString large;
  QString common;
  QString medium;
  QString small;
  QString grid;

  struct Neko {
    static constexpr auto value = NEKO_NAMESPACE::Object(
        "large", &BangumiSubjectImages::large, "common",
        &BangumiSubjectImages::common, "medium", &BangumiSubjectImages::medium,
        "small", &BangumiSubjectImages::small, "grid",
        &BangumiSubjectImages::grid);
  };
};

/**
 * @brief SlimSubject 的标签统计协议/领域值。
 * @pre wire 字段 total_cont 通过 rename_tag 映射；计数范围由分页验证阶段检查。
 * @post 成功验证后 count 和 totalCount 均为非负。
 */
struct BangumiSubjectTag {
  QString name;
  int count = 0;
  int totalCount = 0;

  struct Neko {
    static constexpr auto value = NEKO_NAMESPACE::Object(
        "name", &BangumiSubjectTag::name, "count", &BangumiSubjectTag::count,
        "totalCount",
        NEKO_NAMESPACE::make_tags<NEKO_NAMESPACE::rename_tag<"total_cont">>(
            &BangumiSubjectTag::totalCount));
  };
};

/**
 * @brief 用户收藏中内嵌的 Bangumi SlimSubject 协议/领域对象。
 * @pre Neko 负责字段存在与类型；分页验证负责正 ID、合法枚举和非负计数。
 * @post date 保留协议的 missing/null 语义，不规范化为空字符串。
 */
struct BangumiCollectionSubject {
  std::int64_t id = 0;
  BangumiSubjectType type = BangumiSubjectType::Anime;
  QString name;
  QString nameCn;
  QString shortSummary;
  std::optional<QString> date;
  BangumiSubjectImages images;
  int volumes = 0;
  int episodes = 0;
  int collectionTotal = 0;
  double score = 0;
  int rank = 0;
  std::vector<BangumiSubjectTag> tags;

  struct Neko {
    static constexpr auto value = NEKO_NAMESPACE::Object(
        "id", &BangumiCollectionSubject::id, "type",
        &BangumiCollectionSubject::type, "name", &BangumiCollectionSubject::name,
        "nameCn",
        NEKO_NAMESPACE::make_tags<NEKO_NAMESPACE::rename_tag<"name_cn">>(
            &BangumiCollectionSubject::nameCn),
        "shortSummary",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::rename_tag<"short_summary">>(
            &BangumiCollectionSubject::shortSummary),
        "date", &BangumiCollectionSubject::date, "images",
        &BangumiCollectionSubject::images, "volumes",
        &BangumiCollectionSubject::volumes, "episodes",
        NEKO_NAMESPACE::make_tags<NEKO_NAMESPACE::rename_tag<"eps">>(
            &BangumiCollectionSubject::episodes),
        "collectionTotal",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::rename_tag<"collection_total">>(
            &BangumiCollectionSubject::collectionTotal),
        "score", &BangumiCollectionSubject::score, "rank",
        &BangumiCollectionSubject::rank, "tags",
        &BangumiCollectionSubject::tags);
  };
};

/**
 * @brief Bangumi 用户收藏条目的协议/领域对象。
 * @pre Neko 负责结构类型；分页验证负责评分、进度、枚举和 updatedAt 约束。
 * @post comment/subject 保留 optional 语义，不需要第二套 wire DTO。
 */
struct BangumiUserCollection {
  std::int64_t subjectId = 0;
  BangumiSubjectType subjectType = BangumiSubjectType::Anime;
  int rate = 0;
  BangumiCollectionType collectionType = BangumiCollectionType::Wish;
  std::optional<QString> comment;
  std::vector<QString> tags;
  int episodeStatus = 0;
  int volumeStatus = 0;
  QString updatedAt;
  bool isPrivate = false;
  std::optional<BangumiCollectionSubject> subject;

  struct Neko {
    static constexpr auto value = NEKO_NAMESPACE::Object(
        "subjectId",
        NEKO_NAMESPACE::make_tags<NEKO_NAMESPACE::rename_tag<"subject_id">>(
            &BangumiUserCollection::subjectId),
        "subjectType",
        NEKO_NAMESPACE::make_tags<
            NEKO_NAMESPACE::rename_tag<"subject_type">>(
            &BangumiUserCollection::subjectType),
        "rate", &BangumiUserCollection::rate, "collectionType",
        NEKO_NAMESPACE::make_tags<NEKO_NAMESPACE::rename_tag<"type">>(
            &BangumiUserCollection::collectionType),
        "comment", &BangumiUserCollection::comment, "tags",
        &BangumiUserCollection::tags, "episodeStatus",
        NEKO_NAMESPACE::make_tags<NEKO_NAMESPACE::rename_tag<"ep_status">>(
            &BangumiUserCollection::episodeStatus),
        "volumeStatus",
        NEKO_NAMESPACE::make_tags<NEKO_NAMESPACE::rename_tag<"vol_status">>(
            &BangumiUserCollection::volumeStatus),
        "updatedAt",
        NEKO_NAMESPACE::make_tags<NEKO_NAMESPACE::rename_tag<"updated_at">>(
            &BangumiUserCollection::updatedAt),
        "isPrivate",
        NEKO_NAMESPACE::make_tags<NEKO_NAMESPACE::rename_tag<"private">>(
            &BangumiUserCollection::isPrivate),
        "subject", &BangumiUserCollection::subject);
  };
};

/**
 * @brief Bangumi 用户收藏分页的协议/领域根对象。
 * @pre total/limit/offset/data 必须存在且类型正确；业务验证要求非负且 limit<=50。
 * @post 只有整页和所有嵌套条目均有效时才从解析函数返回。
 */
struct BangumiUserCollectionPage {
  int total = 0;
  int limit = 0;
  int offset = 0;
  std::vector<BangumiUserCollection> data;

  struct Neko {
    static constexpr auto value = NEKO_NAMESPACE::Object(
        "total", &BangumiUserCollectionPage::total, "limit",
        &BangumiUserCollectionPage::limit, "offset",
        &BangumiUserCollectionPage::offset, "data",
        &BangumiUserCollectionPage::data);
  };
};

using BangumiUserCollectionsResponse =
    BangumiResponse<BangumiUserCollectionPage>;

/**
 * @brief 把已验证的领域分页对象编码为 Bangumi wire body。
 * @pre page 及所有子对象满足字段范围，所有浮点值有限。
 * @return 完整 UTF-8 body；后端拒绝某字段时返回空 QByteArray。
 * @post 不修改 page，不向调用方暴露 QJsonObject/QJsonValue。
 */
auto encodeBangumiUserCollectionPage(const BangumiUserCollectionPage &page)
    -> QByteArray;

/** Declaration submitted when the user-collections feature is enabled. */
auto bangumiUserCollectionsFeature() -> BangumiFeatureDeclaration;

namespace detail {

/**
 * @brief 构造用户收藏 GET endpoint。
 * @pre HTTPS base、username、分页范围和可选枚举均有效。
 * @post 成功返回已 percent-encode 的 URL；失败不会产生网络副作用。
 */
auto buildBangumiUserCollectionsUrl(const BangumiSettings &settings,
                                    QStringView username,
                                    const BangumiCollectionQuery &query)
    -> BangumiResult<QUrl>;
/**
 * @brief 解码并完整验证用户收藏响应。
 * @pre data 已通过调用方的最大 body 大小检查。
 * @post 成功值与 JSON DOM 解耦；失败时不提交部分解析结果。
 */
auto parseBangumiUserCollectionsResponse(const QByteArray &data)
    -> BangumiResult<BangumiUserCollectionPage>;

} // namespace detail
} // namespace anime_land

NEKO_BEGIN_NAMESPACE

/**
 * @brief BangumiSubjectType 的严格 numeric enum parser。
 * @pre wire 值必须是整数 1、2、3、4 或 6。
 * @post 无效整数返回 InvalidType，目标枚举保持不变。
 */
template <> struct CustomParser<anime_land::BangumiSubjectType> {
  /**
   * @brief 把有效枚举写成官方 numeric wire 值。
   * @pre value 必须属于 isValidBangumiSubjectType() 集合。
   * @post 无效值返回 InvalidType，writer 不应被视为成功。
   */
  template <typename W, typename Parent, typename Tags>
  static auto write(W &writer, anime_land::BangumiSubjectType value,
                    const Parent &parent, const Tags &tags) -> ParserResult {
    if (!anime_land::isValidBangumiSubjectType(value)) {
      return detail::parser_error(sa::ErrorCode::InvalidType,
                                  "Invalid Bangumi subject type");
    }
    return parser_write<W>(writer, static_cast<int>(value), parent, tags);
  }

  /**
   * @brief 从 numeric wire 值严格读取 subject type。
   * @pre input 必须能无损读取为 int。
   * @post 仅在取值合法时提交 value；失败保持 value 不变。
   */
  template <typename R, typename Tags>
  static auto read(typename R::InputValueType input,
                   anime_land::BangumiSubjectType &value, const Tags &tags)
      -> ParserResult {
    int raw = 0;
    auto result = parser_read<R>(input, raw, tags);
    const auto parsed = static_cast<anime_land::BangumiSubjectType>(raw);
    if (!result || !anime_land::isValidBangumiSubjectType(parsed)) {
      return result ? detail::parser_error(sa::ErrorCode::InvalidType,
                                           "Invalid Bangumi subject type")
                    : result;
    }
    value = parsed;
    return sa::success();
  }

  /** @brief 声明 numeric enum 的基础 schema 为 integer。 */
  static auto toSchema() -> parsing::schema::Type {
    return parser_schema<int>();
  }
};

/**
 * @brief BangumiCollectionType 的严格 numeric enum parser。
 * @pre wire 值必须是整数 1..5。
 * @post 无效整数返回 InvalidType，目标枚举保持不变。
 */
template <> struct CustomParser<anime_land::BangumiCollectionType> {
  /**
   * @brief 把有效枚举写成官方 numeric wire 值。
   * @pre value 必须属于 isValidBangumiCollectionType() 集合。
   * @post 无效值返回 InvalidType，writer 不应被视为成功。
   */
  template <typename W, typename Parent, typename Tags>
  static auto write(W &writer, anime_land::BangumiCollectionType value,
                    const Parent &parent, const Tags &tags) -> ParserResult {
    if (!anime_land::isValidBangumiCollectionType(value)) {
      return detail::parser_error(sa::ErrorCode::InvalidType,
                                  "Invalid Bangumi collection type");
    }
    return parser_write<W>(writer, static_cast<int>(value), parent, tags);
  }

  /**
   * @brief 从 numeric wire 值严格读取 collection type。
   * @pre input 必须能无损读取为 int。
   * @post 仅在取值合法时提交 value；失败保持 value 不变。
   */
  template <typename R, typename Tags>
  static auto read(typename R::InputValueType input,
                   anime_land::BangumiCollectionType &value, const Tags &tags)
      -> ParserResult {
    int raw = 0;
    auto result = parser_read<R>(input, raw, tags);
    const auto parsed = static_cast<anime_land::BangumiCollectionType>(raw);
    if (!result || !anime_land::isValidBangumiCollectionType(parsed)) {
      return result ? detail::parser_error(sa::ErrorCode::InvalidType,
                                           "Invalid Bangumi collection type")
                    : result;
    }
    value = parsed;
    return sa::success();
  }

  /** @brief 声明 numeric enum 的基础 schema 为 integer。 */
  static auto toSchema() -> parsing::schema::Type {
    return parser_schema<int>();
  }
};

NEKO_END_NAMESPACE
