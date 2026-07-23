#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileDevice>
#include <QNetworkProxy>
#include <QTemporaryDir>
#include <QUrlQuery>

#include <ilias/platform/qt.hpp>

#include "bangumi/auth.hpp"
#include "bangumi/bangumi.hpp"
#include "bangumi/capability.hpp"
#include "bangumi/collection.hpp"
#include "bangumi/config.hpp"
#include "bangumi/http_request.hpp"
#include "bangumi/network_proxy.hpp"
#include "bangumi/protocol.hpp"
#include "bangumi/system_credential_provider_p.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

using namespace anime_land;

namespace {

auto sampleToken() -> BangumiToken {
  return {
      .accessToken = "access-secret",
      .refreshToken = "refresh-secret",
      .tokenType = "Bearer",
      .scope = "read",
      .userId = 42,
      .expiresAt = 4'102'444'800,
  };
}

/** 测试可观察的系统 provider 状态；不会访问用户真实凭据库。 */
struct FakeSystemCredentialState {
  std::optional<QByteArray> value;
  int loads = 0;
  int saves = 0;
  int clears = 0;
};

/**
 * @brief 仅供 SystemTokenStore 单元测试使用的内存字节 provider。
 * @invariant mState 非空，且由测试与本 provider 共享所有权。
 */
class FakeSystemCredentialProvider final
    : public anime_land::detail::SystemCredentialProvider {
public:
  /**
   * @brief 构造 fake provider。
   * @pre state 非空。
   * @post provider 与测试共享 state，不访问任何平台服务。
   */
  explicit FakeSystemCredentialProvider(
      std::shared_ptr<FakeSystemCredentialState> state)
      : mState(std::move(state)) {}

  /** @copydoc anime_land::detail::SystemCredentialProvider::load() */
  auto load() -> BangumiResult<std::optional<QByteArray>> override {
    ++mState->loads;
    return mState->value;
  }

  /** @copydoc anime_land::detail::SystemCredentialProvider::save(const QByteArray &) */
  auto save(const QByteArray &data) -> BangumiResult<void> override {
    ++mState->saves;
    mState->value = data;
    return {};
  }

  /** @copydoc anime_land::detail::SystemCredentialProvider::clear() */
  auto clear() -> BangumiResult<void> override {
    ++mState->clears;
    mState->value.reset();
    return {};
  }

private:
  std::shared_ptr<FakeSystemCredentialState> mState;
};

struct RapidJsonProbe {
  QString text;
  std::int64_t count = 0;
  std::optional<QString> note;

  // clang-format off
  struct Neko {
    static constexpr auto value = NEKO_NAMESPACE::Object(
        "text",  &RapidJsonProbe::text,
        "count", &RapidJsonProbe::count,
        "note",  &RapidJsonProbe::note
    );
  };
  // clang-format on
};

struct RapidJsonByteStringProbe {
  std::string text;

  struct Neko {
    static constexpr auto value =
        NEKO_NAMESPACE::Object("text", &RapidJsonByteStringProbe::text);
  };
};

} // namespace

TEST(BangumiProtocol, RapidJsonRoundTripsCompactAndIndentedText) {
  const RapidJsonProbe source{
      .text = QStringLiteral("原生 QString"),
      .count = std::numeric_limits<std::int64_t>::max(),
      .note = std::nullopt,
  };

  const auto compact = anime_land::bangumi_protocol::encode(source);
  ASSERT_TRUE(compact);
  EXPECT_FALSE(compact->contains('\n'));
  EXPECT_TRUE(compact->contains("\"count\":9223372036854775807"));
  EXPECT_FALSE(compact->contains("\"note\""));

  const auto indented = anime_land::bangumi_protocol::encode(
      source, anime_land::bangumi_protocol::JsonFormat::Indented);
  ASSERT_TRUE(indented);
  EXPECT_TRUE(indented->contains('\n'));

  RapidJsonProbe decoded;
  const auto error = anime_land::bangumi_protocol::decode(*compact, decoded);
  EXPECT_FALSE(error);
  EXPECT_EQ(decoded.text, source.text);
  EXPECT_EQ(decoded.count, source.count);
  EXPECT_FALSE(decoded.note);
}

TEST(BangumiProtocol, RapidJsonRejectsInvalidUtf8InUnknownFields) {
  QByteArray input =
      QByteArrayLiteral(R"json({"text":"valid","count":1,"unknown":")json");
  input.append(static_cast<char>(0xC3));
  input.append(QByteArrayLiteral(R"json("})json"));

  RapidJsonProbe decoded{
      .text = QStringLiteral("unchanged"),
      .count = 7,
      .note = QStringLiteral("unchanged"),
  };
  const auto error = anime_land::bangumi_protocol::decode(input, decoded);

  ASSERT_TRUE(error);
  EXPECT_EQ(decoded.text, QStringLiteral("unchanged"));
  EXPECT_EQ(decoded.count, 7);
  ASSERT_TRUE(decoded.note);
  EXPECT_EQ(*decoded.note, QStringLiteral("unchanged"));
}

TEST(BangumiProtocol, RapidJsonRejectsInvalidUtf8OnOutput) {
  RapidJsonByteStringProbe source;
  source.text.push_back(static_cast<char>(0xC3));

  EXPECT_FALSE(anime_land::bangumi_protocol::encode(source));
}

TEST(BangumiAuthFoundation, BuildsAuthorizationUrlWithStateAndRedirect) {
  BangumiSettings settings;
  settings.client_id = QStringLiteral("client-id");
  settings.client_secret = "client-secret";

  auto result = anime_land::detail::buildBangumiAuthorizationUrl(
      settings, QStringLiteral("state-123"));

  ASSERT_TRUE(result) << result.error().message.toStdString();
  EXPECT_EQ(result->scheme(), QStringLiteral("https"));
  EXPECT_EQ(result->host(), QStringLiteral("bgm.tv"));
  EXPECT_EQ(result->path(), QStringLiteral("/oauth/authorize"));
  const QUrlQuery query(*result);
  EXPECT_EQ(query.queryItemValue(QStringLiteral("client_id")),
            QStringLiteral("client-id"));
  EXPECT_EQ(query.queryItemValue(QStringLiteral("response_type")),
            QStringLiteral("code"));
  EXPECT_EQ(query.queryItemValue(QStringLiteral("redirect_uri")),
            settings.redirect_uri.toString());
  EXPECT_EQ(query.queryItemValue(QStringLiteral("state")),
            QStringLiteral("state-123"));
}

TEST(BangumiAuthFoundation, RejectsNonLoopbackRedirect) {
  BangumiSettings settings;
  settings.client_id = QStringLiteral("client-id");
  settings.client_secret = "client-secret";
  settings.redirect_uri =
      QUrl(QStringLiteral("https://example.com/callback"), QUrl::StrictMode);

  auto result = anime_land::detail::buildBangumiAuthorizationUrl(
      settings, QStringLiteral("state-123"));

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, BangumiErrorCode::InvalidConfiguration);
}

TEST(BangumiAuthFoundation, DetectsNonexistentApplicationPage) {
  BangumiSettings settings;
  settings.client_id = QStringLiteral("bgm0000000000000000");
  settings.client_secret = "secret";

  auto result = anime_land::detail::inspectBangumiAuthorizationPage(
      QByteArrayLiteral("<p>app_nonexistence</p>"), settings);

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, BangumiErrorCode::InvalidConfiguration);
  EXPECT_TRUE(result.error().message.contains(QStringLiteral("/dev/app")));
  EXPECT_TRUE(result.error().message.contains(QStringLiteral("38457")));
}

TEST(BangumiAuthFoundation, RejectsWhitespaceInApplicationCredentials) {
  BangumiSettings settings;
  settings.client_id = QStringLiteral(" bgm0000000000000000");
  settings.client_secret = "secret";

  auto result = anime_land::detail::buildBangumiAuthorizationUrl(
      settings, QStringLiteral("state-123"));

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, BangumiErrorCode::InvalidConfiguration);
}

TEST(BangumiAuthFoundation, ParsesValidCallback) {
  const QByteArray request = QByteArrayLiteral(
      "GET /callback?code=authorization-code&state=state-123 HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n\r\n");

  auto result = anime_land::detail::parseBangumiCallbackRequest(
      request, QStringLiteral("/callback"), QStringLiteral("state-123"));

  ASSERT_TRUE(result) << result.error().message.toStdString();
  EXPECT_EQ(*result, QStringLiteral("authorization-code"));
}

TEST(BangumiAuthFoundation, RejectsCallbackWithWrongState) {
  const QByteArray request = QByteArrayLiteral(
      "GET /callback?code=authorization-code&state=wrong HTTP/1.1\r\n\r\n");

  auto result = anime_land::detail::parseBangumiCallbackRequest(
      request, QStringLiteral("/callback"), QStringLiteral("state-123"));

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, BangumiErrorCode::InvalidCallback);
}

TEST(BangumiTokenStore, MemoryStoreRoundTripAndClear) {
  auto created =
      TokenStore::create({.kind = TokenStoreKind::Memory, .filePath = {}});
  ASSERT_TRUE(created) << created.error().message.toStdString();
  auto store = std::move(*created);

  auto saved = store->save(sampleToken()).wait();
  ASSERT_TRUE(saved) << saved.error().message.toStdString();
  auto loaded = store->load().wait();
  ASSERT_TRUE(loaded) << loaded.error().message.toStdString();
  ASSERT_TRUE(*loaded);
  EXPECT_EQ((*loaded)->accessToken, "access-secret");
  EXPECT_EQ((*loaded)->userId, 42);

  auto cleared = store->clear().wait();
  ASSERT_TRUE(cleared) << cleared.error().message.toStdString();
  loaded = store->load().wait();
  ASSERT_TRUE(loaded);
  EXPECT_FALSE(*loaded);
}

TEST(BangumiTokenStore, FileStoreRoundTripUsesOwnerOnlyPermissions) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString path = directory.filePath(QStringLiteral("token.json"));
  auto created =
      TokenStore::create({.kind = TokenStoreKind::File, .filePath = path});
  ASSERT_TRUE(created) << created.error().message.toStdString();
  auto store = std::move(*created);

  auto saved = store->save(sampleToken()).wait();
  ASSERT_TRUE(saved) << saved.error().message.toStdString();
  EXPECT_TRUE(QFile::exists(path));
  const auto permissions = QFile::permissions(path);
  EXPECT_TRUE(permissions.testFlag(QFileDevice::ReadOwner));
  EXPECT_TRUE(permissions.testFlag(QFileDevice::WriteOwner));
  EXPECT_FALSE(permissions.testFlag(QFileDevice::ReadGroup));
  EXPECT_FALSE(permissions.testFlag(QFileDevice::ReadOther));

  auto loaded = store->load().wait();
  ASSERT_TRUE(loaded) << loaded.error().message.toStdString();
  ASSERT_TRUE(*loaded);
  EXPECT_EQ((*loaded)->refreshToken, "refresh-secret");

  auto cleared = store->clear().wait();
  ASSERT_TRUE(cleared) << cleared.error().message.toStdString();
  EXPECT_FALSE(QFile::exists(path));
}

TEST(BangumiTokenStore, SystemStoreRoundTripUsesInjectedPlatformProvider) {
  auto state = std::make_shared<FakeSystemCredentialState>();
  auto store = anime_land::detail::createSystemTokenStore(
      std::make_unique<FakeSystemCredentialProvider>(state));

  auto saved = store->save(sampleToken()).wait();
  ASSERT_TRUE(saved) << saved.error().message.toStdString();
  ASSERT_TRUE(state->value);
  EXPECT_EQ(state->saves, 1);

  auto loaded = store->load().wait();
  ASSERT_TRUE(loaded) << loaded.error().message.toStdString();
  ASSERT_TRUE(*loaded);
  EXPECT_EQ((*loaded)->accessToken, QStringLiteral("access-secret"));
  EXPECT_EQ((*loaded)->refreshToken, QStringLiteral("refresh-secret"));
  EXPECT_EQ(state->loads, 1);

  auto cleared = store->clear().wait();
  ASSERT_TRUE(cleared) << cleared.error().message.toStdString();
  EXPECT_FALSE(state->value);
  EXPECT_EQ(state->clears, 1);
}

TEST(BangumiTokenStore, DefaultsToSystemAndConstructsPlatformBackendLazily) {
  EXPECT_EQ(TokenStoreOptions{}.kind, TokenStoreKind::System);

  auto created = TokenStore::create(TokenStoreOptions{});
  ASSERT_TRUE(created) << created.error().message.toStdString();
}

TEST(BangumiTokenStore, RejectsTokenFileForMemoryStore) {
  auto created = TokenStore::create(
      {.kind = TokenStoreKind::Memory,
       .filePath = QStringLiteral("/tmp/must-not-be-used.json")});

  ASSERT_FALSE(created);
  EXPECT_EQ(created.error().code, BangumiErrorCode::InvalidConfiguration);
}

TEST(BangumiTokenStore, ClearsSensitiveStrings) {
  auto token = sampleToken();

  clearBangumiToken(token);

  EXPECT_TRUE(token.accessToken.isEmpty());
  EXPECT_TRUE(token.refreshToken.isEmpty());
  EXPECT_TRUE(token.tokenType.isEmpty());
  EXPECT_FALSE(token.scope);
  EXPECT_EQ(token.userId, 0);
  EXPECT_EQ(token.expiresAt, 0);
}

TEST(BangumiOAuthApplication, CanBeProvidedAfterModuleConstruction) {
  auto store = TokenStore::create(TokenStoreOptions{});
  ASSERT_TRUE(store) << store.error().message.toStdString();

  BangumiModule module(BangumiSettings{}, std::move(*store));
  EXPECT_FALSE(module.hasOAuthApplication());

  BangumiOAuthApplication application{
      .clientId = QStringLiteral("client-id"),
      .clientSecret = "client-secret",
  };
  module.setOAuthApplication(application);
  EXPECT_TRUE(module.hasOAuthApplication());

  clearBangumiOAuthApplication(application);
  EXPECT_TRUE(application.clientId.isEmpty());
  EXPECT_TRUE(application.clientSecret.empty());
}

TEST(BangumiNetworkProxy, KeepsExistingPolicyWhenCustomProxyIsEmpty) {
  QNetworkAccessManager network;
  network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));

  auto configured =
      anime_land::detail::configureBangumiNetworkProxy(network,
                                                       BangumiSettings{});

  ASSERT_TRUE(configured) << configured.error().message.toStdString();
  EXPECT_FALSE(*configured);
  EXPECT_EQ(network.proxy().type(), QNetworkProxy::NoProxy);
}

TEST(BangumiNetworkProxy, AppliesAuthenticatedHttpProxy) {
  QNetworkAccessManager network;
  BangumiSettings settings;
  settings.proxy_url =
      QUrl(QStringLiteral("http://proxy.example:7890"), QUrl::StrictMode);
  settings.proxy_username = QStringLiteral("proxy-user");
  settings.proxy_password = "proxy-password";

  auto configured =
      anime_land::detail::configureBangumiNetworkProxy(network, settings);

  ASSERT_TRUE(configured) << configured.error().message.toStdString();
  EXPECT_TRUE(*configured);
  EXPECT_EQ(network.proxy().type(), QNetworkProxy::HttpProxy);
  EXPECT_EQ(network.proxy().hostName(), QStringLiteral("proxy.example"));
  EXPECT_EQ(network.proxy().port(), 7890);
  EXPECT_EQ(network.proxy().user(), QStringLiteral("proxy-user"));
  EXPECT_EQ(network.proxy().password(), QStringLiteral("proxy-password"));
}

TEST(BangumiNetworkProxy, AppliesSocks5ProxyWithDefaultPort) {
  QNetworkAccessManager network;
  BangumiSettings settings;
  settings.proxy_url =
      QUrl(QStringLiteral("socks5://127.0.0.1"), QUrl::StrictMode);

  auto configured =
      anime_land::detail::configureBangumiNetworkProxy(network, settings);

  ASSERT_TRUE(configured) << configured.error().message.toStdString();
  EXPECT_TRUE(*configured);
  EXPECT_EQ(network.proxy().type(), QNetworkProxy::Socks5Proxy);
  EXPECT_EQ(network.proxy().hostName(), QStringLiteral("127.0.0.1"));
  EXPECT_EQ(network.proxy().port(), 1080);
}

TEST(BangumiNetworkProxy, ModuleAppliesProxyToItsSingleSharedNetworkManager) {
  auto store = TokenStore::create(
      {.kind = TokenStoreKind::Memory, .filePath = QString{}});
  ASSERT_TRUE(store) << store.error().message.toStdString();
  BangumiSettings settings;
  settings.proxy_url =
      QUrl(QStringLiteral("http://127.0.0.1:7890"), QUrl::StrictMode);

  BangumiModule module(std::move(settings), std::move(*store));

  const auto networks = module.findChildren<QNetworkAccessManager *>(
      QString{}, Qt::FindDirectChildrenOnly);
  ASSERT_EQ(networks.size(), 1);
  EXPECT_EQ(networks.front()->proxy().type(), QNetworkProxy::HttpProxy);
  EXPECT_EQ(networks.front()->proxy().hostName(), QStringLiteral("127.0.0.1"));
  EXPECT_EQ(networks.front()->proxy().port(), 7890);
}

TEST(BangumiNetworkProxy, RejectsUnsupportedSchemeWithoutChangingNetwork) {
  QNetworkAccessManager network;
  network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
  BangumiSettings settings;
  settings.proxy_url =
      QUrl(QStringLiteral("https://proxy.example:7890"), QUrl::StrictMode);

  auto configured =
      anime_land::detail::configureBangumiNetworkProxy(network, settings);

  ASSERT_FALSE(configured);
  EXPECT_EQ(configured.error().code, BangumiErrorCode::InvalidConfiguration);
  EXPECT_EQ(network.proxy().type(), QNetworkProxy::NoProxy);
}

TEST(BangumiCapabilities, UsesComposableFlagsAndDynamicFeatureGuide) {
  const auto readAndWrite =
      BangumiCapability::CollectionRead | BangumiCapability::CollectionWrite;
  EXPECT_TRUE(
      hasBangumiCapability(readAndWrite, BangumiCapability::CollectionRead));
  EXPECT_TRUE(
      hasBangumiCapability(readAndWrite, BangumiCapability::CollectionWrite));
  EXPECT_FALSE(hasBangumiCapability(readAndWrite, BangumiCapability::WikiRead));

  BangumiModuleOptions options;
  options.features.push_back(bangumiUserCollectionsFeature());
  const auto guide =
      buildBangumiOAuthApplicationGuide(BangumiSettings{}, options);
  ASSERT_EQ(guide.requiredCapabilities.size(), 1U);
  EXPECT_EQ(guide.requiredCapabilities.front().capability.capability,
            BangumiCapability::CollectionRead);
  ASSERT_EQ(guide.requiredCapabilities.front().features.size(), 1U);
  EXPECT_EQ(guide.requiredCapabilities.front().features.front().id,
            "user_collections.read");
  EXPECT_EQ(guide.optionalCapabilities.size(), 7U);
  EXPECT_EQ(guide.applicationPageUrl,
            QUrl(QStringLiteral("https://bgm.tv/dev/app")));
  EXPECT_FALSE(guide.requiresCrossOrigin);

  auto error = missingBangumiCapabilityError(BangumiCapability::CollectionRead,
                                             bangumiUserCollectionsFeature(),
                                             BangumiSettings{});
  EXPECT_EQ(error.code, BangumiErrorCode::MissingCapability);
  ASSERT_TRUE(error.remediation);
  EXPECT_EQ(error.remediation->capability, BangumiCapability::CollectionRead);
  EXPECT_EQ(error.remediation->featureId, "user_collections.read");
  EXPECT_TRUE(
      error.remediation->permissionName.contains(QStringLiteral("收藏 READ")));
  EXPECT_EQ(error.remediation->applicationSettingsUrl,
            QUrl(QStringLiteral("https://bgm.tv/dev/app")));
}

TEST(BangumiCollections, BuildsFilteredPagedRequest) {
  BangumiCollectionQuery query{
      .subjectType = BangumiSubjectType::Anime,
      .collectionType = BangumiCollectionType::Doing,
      .limit = 50,
      .offset = 100,
  };

  auto result = anime_land::detail::buildBangumiUserCollectionsUrl(
      BangumiSettings{}, QStringLiteral("user/name"), query);

  ASSERT_TRUE(result) << result.error().message.toStdString();
  EXPECT_EQ(result->path(), QStringLiteral("/v0/users/user/name/collections"));
  const QUrlQuery parameters(*result);
  EXPECT_EQ(parameters.queryItemValue(QStringLiteral("subject_type")),
            QStringLiteral("2"));
  EXPECT_EQ(parameters.queryItemValue(QStringLiteral("type")),
            QStringLiteral("3"));
  EXPECT_EQ(parameters.queryItemValue(QStringLiteral("limit")),
            QStringLiteral("50"));
  EXPECT_EQ(parameters.queryItemValue(QStringLiteral("offset")),
            QStringLiteral("100"));
}

TEST(BangumiCollections, ParsesOfficialPagedCollectionShape) {
  const QByteArray response = QByteArrayLiteral(R"json({
    "total": 1,
    "limit": 30,
    "offset": 0,
    "data": [{
      "subject_id": 8,
      "subject_type": 2,
      "rate": 9,
      "type": 3,
      "comment": "重看中",
      "tags": ["科幻"],
      "ep_status": 5,
      "vol_status": 0,
      "updated_at": "2022-06-19T18:44:13.6140127+08:00",
      "private": true,
      "subject": {
        "id": 8,
        "type": 2,
        "name": "Example",
        "name_cn": "示例",
        "short_summary": "简介",
        "date": "2022-01-01",
        "images": {
          "large": "large", "common": "common", "medium": "medium",
          "small": "small", "grid": "grid"
        },
        "volumes": 0,
        "eps": 12,
        "collection_total": 100,
        "score": 8.2,
        "rank": 10,
        "tags": [{"name": "动画", "count": 20, "total_cont": 30}]
      }
    }]
  })json");

  auto result =
      anime_land::detail::parseBangumiUserCollectionsResponse(response);

  ASSERT_TRUE(result) << result.error().message.toStdString();
  EXPECT_EQ(result->total, 1);
  ASSERT_EQ(result->data.size(), 1U);
  EXPECT_EQ(result->data.front().subjectId, 8);
  EXPECT_EQ(result->data.front().collectionType, BangumiCollectionType::Doing);
  ASSERT_TRUE(result->data.front().comment);
  EXPECT_EQ(*result->data.front().comment, QStringLiteral("重看中"));
  EXPECT_TRUE(result->data.front().isPrivate);
  ASSERT_TRUE(result->data.front().subject);
  ASSERT_TRUE(result->data.front().subject->date);
  EXPECT_EQ(*result->data.front().subject->date,
            QStringLiteral("2022-01-01"));
  EXPECT_EQ(result->data.front().subject->nameCn, QStringLiteral("示例"));
  ASSERT_EQ(result->data.front().subject->tags.size(), 1U);
  EXPECT_EQ(result->data.front().subject->tags.front().totalCount, 30);

  const auto serialized = encodeBangumiUserCollectionPage(*result);
  auto roundTrip =
      anime_land::detail::parseBangumiUserCollectionsResponse(serialized);
  ASSERT_TRUE(roundTrip) << roundTrip.error().message.toStdString();
  ASSERT_EQ(roundTrip->data.size(), 1U);
  EXPECT_EQ(roundTrip->data.front().subjectId, 8);
}

TEST(BangumiCollections, RejectsInvalidProtocolEnumTransactionally) {
  const QByteArray response = QByteArrayLiteral(R"json({
    "total": 1,
    "limit": 30,
    "offset": 0,
    "data": [{
      "subject_id": 8,
      "subject_type": 5,
      "rate": 9,
      "type": 3,
      "tags": [],
      "ep_status": 0,
      "vol_status": 0,
      "updated_at": "2022-06-19T18:44:13+08:00",
      "private": false
    }]
  })json");

  auto result =
      anime_land::detail::parseBangumiUserCollectionsResponse(response);

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, BangumiErrorCode::InvalidResponse);
}

TEST(BangumiHttpRequest, MaterializesTypedHeadersAndFormBody) {
  const BangumiTokenExchangeRequest exchange{
      .clientId = QStringLiteral("client id"),
      .clientSecret = QStringLiteral("secret&value"),
      .code = QStringLiteral("code"),
      .redirectUri = QStringLiteral("http://127.0.0.1/callback"),
      .state = QStringLiteral("state"),
  };
  const BangumiHttpRequest value{
      .url = QUrl(QStringLiteral("https://bgm.tv/oauth/access_token")),
      .headers = {.userAgent = QStringLiteral("anime-land/test"),
                  .bearerToken = QStringLiteral("token"),
                  .contentType =
                      QByteArrayLiteral("application/x-www-form-urlencoded")},
      .body = exchange.toFormData(),
  };

  const QNetworkRequest request = value.toQt();
  EXPECT_EQ(request.rawHeader(QByteArrayLiteral("Accept")),
            QByteArrayLiteral("application/json"));
  EXPECT_EQ(request.rawHeader(QByteArrayLiteral("Authorization")),
            QByteArrayLiteral("Bearer token"));
  EXPECT_EQ(request.transferTimeout(), 30'000);
  EXPECT_TRUE(value.body.contains("client_id=client%20id"));
  EXPECT_TRUE(value.body.contains("client_secret=secret%26value"));
}

TEST(BangumiCollections, RejectsOutOfRangePagination) {
  BangumiCollectionQuery query;
  query.limit = 51;

  auto result = anime_land::detail::buildBangumiUserCollectionsUrl(
      BangumiSettings{}, QStringLiteral("user"), query);

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, BangumiErrorCode::InvalidConfiguration);
}

#define EXPAND_IN_MAIN_WITH_ARGS(argc, argv)                                   \
  QCoreApplication qtApplication(argc, argv);                                  \
  ilias::QIoContext ioContext;                                                 \
  ioContext.install()
#include "../common/common_main.hpp.in"
