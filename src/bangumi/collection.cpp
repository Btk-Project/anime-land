#include "bangumi/collection.hpp"
#include "bangumi/protocol.hpp"

#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <utility>

namespace anime_land {
namespace {

/**
 * @brief 构造用户收藏响应的统一领域错误。
 * @pre message 不得包含 access token 或未清洗的完整响应 body。
 * @post 返回 InvalidResponse，不修改任何已公开领域对象。
 */
auto invalidCollectionResponse(QString message)
    -> BangumiResult<BangumiUserCollectionPage> {
  return ilias::Err(
      bangumiError(BangumiErrorCode::InvalidResponse, std::move(message)));
}

/**
 * @brief 验证 SlimSubject 的非类型业务约束。
 * @pre subject 已由 Neko 反射完整解码，枚举已经由严格 parser 验证。
 * @post 不修改 subject；任一 ID、计数或评分非法时返回 false。
 */
auto isValidSubject(const BangumiCollectionSubject &subject) -> bool {
  if (subject.id <= 0 || !isValidBangumiSubjectType(subject.type) ||
      subject.volumes < 0 || subject.episodes < 0 ||
      subject.collectionTotal < 0 || !std::isfinite(subject.score) ||
      subject.rank < 0) {
    return false;
  }
  return std::ranges::all_of(subject.tags, [](const BangumiSubjectTag &tag) {
    return tag.count >= 0 && tag.totalCount >= 0;
  });
}

/**
 * @brief 验证一个公开收藏对象的业务约束。
 * @pre collection 已由 Neko 反射完整解码，枚举已经由严格 parser 验证。
 * @post 不修改 collection；可选 subject 也必须整体有效。
 */
auto isValidCollection(const BangumiUserCollection &collection) -> bool {
  return collection.subjectId > 0 &&
         isValidBangumiSubjectType(collection.subjectType) &&
         collection.rate >= 0 && collection.rate <= 10 &&
         isValidBangumiCollectionType(collection.collectionType) &&
         collection.episodeStatus >= 0 && collection.volumeStatus >= 0 &&
         !collection.updatedAt.isEmpty() &&
         (!collection.subject || isValidSubject(*collection.subject));
}

/**
 * @brief 验证公开分页对象及其全部条目。
 * @pre page 已由 Neko 反射完整解码。
 * @post 不修改 page；任一条目无效时整体返回 false。
 */
auto isValidPage(const BangumiUserCollectionPage &page) -> bool {
  return page.total >= 0 && page.limit >= 0 && page.limit <= 50 &&
         page.offset >= 0 &&
         std::ranges::all_of(page.data, isValidCollection);
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
 * @brief 编码已验证的用户收藏分页对象。
 * @pre page 及其全部子对象满足 Bangumi 领域约束，浮点数必须有限。
 * @return 缩进格式的完整 UTF-8 body；编码失败返回空 QByteArray。
 * @post 不修改 page，且业务调用方不需要操作 QJsonDocument。
 */
auto encodeBangumiUserCollectionPage(const BangumiUserCollectionPage &page)
    -> QByteArray {
  if (!isValidPage(page)) {
    return {};
  }
  return bangumi_protocol::encode(page, QJsonDocument::Indented)
      .value_or(QByteArray{});
}

namespace detail {

/**
 * @brief 构造用户收藏 endpoint URL 与 query。
 * @pre settings.bangumi_api 是 HTTPS；username 非空且不超过 64 字符；
 * query.limit 为 1..50，offset 非负，枚举值合法。
 * @post 成功返回 percent-encoded URL；失败不发送请求。
 */
auto buildBangumiUserCollectionsUrl(const BangumiSettings &settings,
                                    QStringView username,
                                    const BangumiCollectionQuery &query)
    -> BangumiResult<QUrl> {
  const QUrl &base = settings.bangumi_api;
  if (!base.isValid() || base.scheme() != QStringLiteral("https") ||
      base.host().isEmpty()) {
    return ilias::Err(
        bangumiError(BangumiErrorCode::InvalidConfiguration,
                     QStringLiteral("bangumi_api 必须是有效的 HTTPS URL")));
  }
  if (username.isEmpty() || username.size() > 64 || query.limit < 1 ||
      query.limit > 50 || query.offset < 0 ||
      (query.subjectType && !isValidBangumiSubjectType(*query.subjectType)) ||
      (query.collectionType &&
       !isValidBangumiCollectionType(*query.collectionType))) {
    return ilias::Err(bangumiError(
        BangumiErrorCode::InvalidConfiguration,
        QStringLiteral(
            "用户收藏查询参数无效（limit 必须为 1..50，offset 不能为负）")));
  }

  const QByteArray encodedUsername =
      QUrl::toPercentEncoding(username.toString());
  QUrl url = base.resolved(
      QUrl::fromEncoded(QByteArrayLiteral("/v0/users/") + encodedUsername +
                        QByteArrayLiteral("/collections")));
  QUrlQuery parameters;
  if (query.subjectType) {
    parameters.addQueryItem(
        QStringLiteral("subject_type"),
        QString::number(static_cast<int>(*query.subjectType)));
  }
  if (query.collectionType) {
    parameters.addQueryItem(
        QStringLiteral("type"),
        QString::number(static_cast<int>(*query.collectionType)));
  }
  parameters.addQueryItem(QStringLiteral("limit"),
                          QString::number(query.limit));
  parameters.addQueryItem(QStringLiteral("offset"),
                          QString::number(query.offset));
  url.setQuery(parameters);
  return url;
}

/**
 * @brief 把用户收藏响应 body 解码并验证为领域分页对象。
 * @pre data 大小已经由网络层限制，且不得在错误日志中原样输出。
 * @post 成功后返回值不依赖 JSON DOM；任一条目失败时不返回部分分页数据。
 */
auto parseBangumiUserCollectionsResponse(const QByteArray &data)
    -> BangumiResult<BangumiUserCollectionPage> {
  BangumiUserCollectionPage page;
  if (auto error = bangumi_protocol::decode(data, page)) {
    return invalidCollectionResponse(
        QStringLiteral("用户收藏响应不是有效 JSON：%1")
            .arg(*error));
  }
  if (!isValidPage(page)) {
    return invalidCollectionResponse(QStringLiteral(
        "用户收藏响应包含越界分页、非法枚举或无效条目"));
  }
  return page;
}

} // namespace detail
} // namespace anime_land
