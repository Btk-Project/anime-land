#include "bangumi/collection.hpp"
#include "bangumi/protocol.hpp"

#include <QUrlQuery>

#include <utility>

namespace anime_land {
namespace {

/**
 * @brief 构造用户收藏响应的统一领域错误。
 * @pre message 不得包含 access token 或未清洗的完整响应 body。
 * @post 返回 InvalidResponse，不修改任何已公开领域对象。
 */
auto invalidCollectionResponse(QString message) -> BangumiResult<BangumiUserCollectionPage> {
    return ilias::Err(bangumiError(BangumiErrorCode::InvalidResponse, std::move(message)));
}

} // namespace

auto bangumiUserCollectionsFeature() -> BangumiFeatureDeclaration {
    return {
        .id = "user_collections.read",
        .name = QStringLiteral("获取用户收藏"),
        .description = QStringLiteral("读取当前账号的收藏、评分与观看进度"),
        .requiredCapabilities = BangumiCapability::CollectionRead,
    };
}

/**
 * @brief 编码用户收藏分页对象。
 * @pre page 及其全部子对象可由协议 serializer 表达。
 * @return 缩进格式的完整 UTF-8 body；编码失败返回空 QByteArray。
 * @post 不修改 page，且业务调用方不需要操作 JSON DOM。
 */
auto encodeBangumiUserCollectionPage(const BangumiUserCollectionPage &page) -> QByteArray {
    return bangumi_protocol::encode(page, bangumi_protocol::JsonFormat::Indented).value_or(QByteArray {});
}

namespace detail {

/**
 * @brief 构造用户收藏 endpoint URL 与 query。
 * @pre settings.bangumi_api 是 HTTPS；username 非空且不超过 64 字符；
 * query.limit 为 1..50，offset 非负。
 * @post 成功返回 percent-encoded URL；失败不发送请求。
 */
auto buildBangumiUserCollectionsUrl(const BangumiSettings &settings, QStringView username, const BangumiCollectionQuery &query) -> BangumiResult<QUrl> {
    const QUrl &base = settings.bangumi_api;
    if (!base.isValid() || base.scheme() != QStringLiteral("https") ||
        base.host().isEmpty()) {
        return ilias::Err(bangumiError(BangumiErrorCode::InvalidConfiguration, QStringLiteral("bangumi_api 必须是有效的 HTTPS URL")));
    }
    if (username.isEmpty() || username.size() > 64 || query.limit < 1 ||
        query.limit > 50 || query.offset < 0) {
        auto message = QStringLiteral("用户收藏查询参数无效（limit 必须为 1..50，offset 不能为负）");
        return ilias::Err(bangumiError(BangumiErrorCode::InvalidConfiguration, message));
    }

    const QByteArray encodedUsername = QUrl::toPercentEncoding(username.toString());
    const QByteArray path = QByteArrayLiteral("/v0/users/") + encodedUsername + QByteArrayLiteral("/collections");
    QUrl url = base.resolved(QUrl::fromEncoded(path));
    QUrlQuery parameters;
    if (query.subjectType) {
        parameters.addQueryItem(QStringLiteral("subject_type"), QString::number(static_cast<int>(*query.subjectType)));
    }
    if (query.collectionType) {
        parameters.addQueryItem(QStringLiteral("type"), QString::number(static_cast<int>(*query.collectionType)));
    }
    parameters.addQueryItem(QStringLiteral("limit"), QString::number(query.limit));
    parameters.addQueryItem(QStringLiteral("offset"), QString::number(query.offset));
    url.setQuery(parameters);
    return url;
}

/**
 * @brief 把用户收藏响应 body 解码为领域分页对象。
 * @pre data 大小已经由网络层限制，且不得在错误日志中原样输出。
 * @post 成功后返回值不依赖 JSON DOM；结构或字段类型错误时失败。
 */
auto parseBangumiUserCollectionsResponse(const QByteArray &data) -> BangumiResult<BangumiUserCollectionPage> {
    BangumiUserCollectionPage page;
    if (auto error = bangumi_protocol::decode(data, page)) {
        auto message = QStringLiteral("用户收藏响应不是有效 JSON：%1").arg(*error);
        return invalidCollectionResponse(message);
    }
    return page;
}

} // namespace detail
} // namespace anime_land
