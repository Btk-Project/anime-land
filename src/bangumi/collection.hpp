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
using namespace NEKO_NAMESPACE;

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

    // clang-format off
    struct Neko {
        static constexpr auto value = Object(
            "large",  &BangumiSubjectImages::large,
            "common", &BangumiSubjectImages::common,
            "medium", &BangumiSubjectImages::medium,
            "small",  &BangumiSubjectImages::small,
            "grid",   &BangumiSubjectImages::grid
        );
    };
    // clang-format on
};

/**
 * @brief SlimSubject 的标签统计协议/领域值。
 * @pre wire 字段 total_cont 通过 rename_tag 映射。
 * @post 数值按服务端原样保留。
 */
struct BangumiSubjectTag {
    QString name;
    int count = 0;
    int totalCount = 0;

    // clang-format off
    struct Neko {
        static constexpr auto value = Object(
            "name",       &BangumiSubjectTag::name,
            "count",      &BangumiSubjectTag::count,
            "totalCount", make_tags<rename_tag<"total_cont">>(&BangumiSubjectTag::totalCount)
        );
    };
    // clang-format on
};

/**
 * @brief 用户收藏中内嵌的 Bangumi SlimSubject 协议/领域对象。
 * @pre Neko 负责字段存在与类型；未知 numeric enum 按原值保留。
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

    // clang-format off
    struct Neko {
        static constexpr auto value = Object(
            "id",              &BangumiCollectionSubject::id,
            "type",            &BangumiCollectionSubject::type,
            "name",            &BangumiCollectionSubject::name,
            "nameCn",          make_tags<rename_tag<"name_cn">>(&BangumiCollectionSubject::nameCn),
            "shortSummary",    make_tags<rename_tag<"short_summary">>(&BangumiCollectionSubject::shortSummary),
            "date",            &BangumiCollectionSubject::date,
            "images",          &BangumiCollectionSubject::images,
            "volumes",         &BangumiCollectionSubject::volumes,
            "episodes",        make_tags<rename_tag<"eps">>(&BangumiCollectionSubject::episodes),
            "collectionTotal", make_tags<rename_tag<"collection_total">>(&BangumiCollectionSubject::collectionTotal),
            "score",           &BangumiCollectionSubject::score,
            "rank",            &BangumiCollectionSubject::rank,
            "tags",            &BangumiCollectionSubject::tags
        );
    };
    // clang-format on
};

/**
 * @brief Bangumi 用户收藏条目的协议/领域对象。
 * @pre Neko 负责结构类型；服务端数值不做本地白名单约束。
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

    // clang-format off
    struct Neko {
        static constexpr auto value = Object(
            "subjectId",      make_tags<rename_tag<"subject_id">>(&BangumiUserCollection::subjectId),
            "subjectType",    make_tags<rename_tag<"subject_type">>(&BangumiUserCollection::subjectType),
            "rate",           &BangumiUserCollection::rate,
            "collectionType", make_tags<rename_tag<"type">>(&BangumiUserCollection::collectionType),
            "comment",        &BangumiUserCollection::comment,
            "tags",           &BangumiUserCollection::tags,
            "episodeStatus",  make_tags<rename_tag<"ep_status">>(&BangumiUserCollection::episodeStatus),
            "volumeStatus",   make_tags<rename_tag<"vol_status">>(&BangumiUserCollection::volumeStatus),
            "updatedAt",      make_tags<rename_tag<"updated_at">>(&BangumiUserCollection::updatedAt),
            "isPrivate",      make_tags<rename_tag<"private">>(&BangumiUserCollection::isPrivate),
            "subject",        &BangumiUserCollection::subject
        );
    };
    // clang-format on
};

/**
 * @brief Bangumi 用户收藏分页的协议/领域根对象。
 * @pre total/limit/offset/data 必须存在且类型正确。
 * @post 解码成功后保留服务端返回的字段值。
 */
struct BangumiUserCollectionPage {
    int total = 0;
    int limit = 0;
    int offset = 0;
    std::vector<BangumiUserCollection> data;

    // clang-format off
    struct Neko {
        static constexpr auto value = Object(
            "total",  &BangumiUserCollectionPage::total,
            "limit",  &BangumiUserCollectionPage::limit,
            "offset", &BangumiUserCollectionPage::offset,
            "data",   &BangumiUserCollectionPage::data
        );
    };
    // clang-format on
};

using BangumiUserCollectionsResponse = BangumiResponse<BangumiUserCollectionPage>;

/**
 * @brief 把领域分页对象编码为 Bangumi wire body。
 * @pre page 及所有子对象可由协议 serializer 表达。
 * @return 完整 UTF-8 body；后端拒绝某字段时返回空 QByteArray。
 * @post 不修改 page，不向调用方暴露 JSON DOM。
 */
auto encodeBangumiUserCollectionPage(const BangumiUserCollectionPage &page) -> QByteArray;

/** Declaration submitted when the user-collections feature is enabled. */
auto bangumiUserCollectionsFeature() -> BangumiFeatureDeclaration;

namespace detail {

/**
 * @brief 构造用户收藏 GET endpoint。
 * @pre HTTPS base、username 和分页范围有效。
 * @post 成功返回已 percent-encode 的 URL；失败不会产生网络副作用。
 */
auto buildBangumiUserCollectionsUrl(const BangumiSettings &settings, QStringView username, const BangumiCollectionQuery &query) -> BangumiResult<QUrl>;
/**
 * @brief 解码用户收藏响应。
 * @pre data 已通过调用方的最大 body 大小检查。
 * @post 成功值与 JSON DOM 解耦；结构或字段类型错误时失败。
 */
auto parseBangumiUserCollectionsResponse(const QByteArray &data) -> BangumiResult<BangumiUserCollectionPage>;

} // namespace detail
} // namespace anime_land

NEKO_BEGIN_NAMESPACE

/**
 * @brief BangumiSubjectType 的开放 numeric enum parser。
 * @pre wire 值必须能无损读取为 int。
 * @post 保留未知整数，避免服务端扩展枚举导致整页解析失败。
 */
template <>
struct CustomParser<anime_land::BangumiSubjectType> {
    /**
     * @brief 把枚举按底层整数写入 wire。
     */
    template <typename W, typename Parent, typename Tags>
    static auto write(W &writer, anime_land::BangumiSubjectType value, const Parent &parent, const Tags &tags) -> ParserResult {
        return parser_write<W>(writer, static_cast<int>(value), parent, tags);
    }

    /**
     * @brief 从 numeric wire 值读取 subject type。
     * @pre input 必须能无损读取为 int。
     * @post 类型读取成功时提交原始整数；类型失败时保持 value 不变。
     */
    template <typename R, typename Tags>
    static auto read(typename R::InputValueType input, anime_land::BangumiSubjectType &value, const Tags &tags) -> ParserResult {
        int raw = 0;
        auto result = parser_read<R>(input, raw, tags);
        if (!result) {
            return result;
        }
        value = static_cast<anime_land::BangumiSubjectType>(raw);
        return sa::success();
    }

    /** @brief 声明 numeric enum 的基础 schema 为 integer。 */
    static auto toSchema() -> parsing::schema::Type {
        return parser_schema<int>();
    }
};

/**
 * @brief BangumiCollectionType 的开放 numeric enum parser。
 * @pre wire 值必须能无损读取为 int。
 * @post 保留未知整数，避免服务端扩展枚举导致整页解析失败。
 */
template <>
struct CustomParser<anime_land::BangumiCollectionType> {
    /**
     * @brief 把枚举按底层整数写入 wire。
     */
    template <typename W, typename Parent, typename Tags>
    static auto write(W &writer, anime_land::BangumiCollectionType value, const Parent &parent, const Tags &tags) -> ParserResult {
        return parser_write<W>(writer, static_cast<int>(value), parent, tags);
    }

    /**
     * @brief 从 numeric wire 值读取 collection type。
     * @pre input 必须能无损读取为 int。
     * @post 类型读取成功时提交原始整数；类型失败时保持 value 不变。
     */
    template <typename R, typename Tags>
    static auto read(typename R::InputValueType input, anime_land::BangumiCollectionType &value, const Tags &tags) -> ParserResult {
        int raw = 0;
        auto result = parser_read<R>(input, raw, tags);
        if (!result) {
            return result;
        }
        value = static_cast<anime_land::BangumiCollectionType>(raw);
        return sa::success();
    }

    /** @brief 声明 numeric enum 的基础 schema 为 integer。 */
    static auto toSchema() -> parsing::schema::Type {
        return parser_schema<int>();
    }
};

NEKO_END_NAMESPACE
