#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTimer>

#include <ilias/platform/qt.hpp>

#include "adapters/episode_provider_js/plugin_runtime.hpp"
#include "model/episode_resource/episode_resource.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

using namespace anime_land;
using namespace anime_land::episode_provider_js;
using namespace std::chrono_literals;

namespace {

class StubProvider final : public EpisodeProvider {
public:
    explicit StubProvider(QString providerKey)
        : mKey(std::move(providerKey)) {}

    auto key() const -> QString override { return mKey; }
    auto name() const -> QString override { return QStringLiteral("Stub"); }
    auto icon() const -> QUrl override { return {}; }
    auto generation() const -> std::uint64_t override { return 7; }
    auto ping() -> ilias::Task<EpisodeProviderResult<ProviderHealth>> override {
        co_return ProviderHealth {
            .reachable = true,
            .mirrorId = {},
            .detail = {},
            .latencyMilliseconds = 0,
        };
    }
    auto search(EpisodeQuery)
        -> ilias::Task<EpisodeProviderResult<std::vector<OnlinePlayable>>> override {
        ++searchCount;
        co_return searchResults;
    }
    auto resolve(OnlinePlayable playable)
        -> ilias::Task<EpisodeProviderResult<OnlinePlayable>> override {
        co_return playable;
    }
    void cancel() override { cancelled = true; }

    bool cancelled = false;
    int searchCount = 0;
    std::vector<OnlinePlayable> searchResults;

private:
    QString mKey;
};

struct FakeHttpResponse {
    int status = 200;
    QByteArray body;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
};

class FakeNetworkReply final : public QNetworkReply {
public:
    FakeNetworkReply(const QNetworkRequest &request, FakeHttpResponse response,
                     QObject *parent)
        : QNetworkReply(parent), mBody(std::move(response.body)) {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
        setRawHeader(QByteArrayLiteral("Content-Type"),
                     QByteArrayLiteral("text/html; charset=utf-8"));
        if (response.error != QNetworkReply::NoError) {
            setError(response.error, QStringLiteral("fake network error"));
        }
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this]() {
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }

    void abort() override {
        if (isFinished()) {
            return;
        }
        setError(QNetworkReply::OperationCanceledError,
                 QStringLiteral("fake request cancelled"));
        setFinished(true);
        emit finished();
    }

    auto bytesAvailable() const -> qint64 override {
        return static_cast<qint64>(mBody.size() - mOffset) +
               QNetworkReply::bytesAvailable();
    }

protected:
    auto readData(char *data, qint64 maximumSize) -> qint64 override {
        if (mOffset >= mBody.size()) {
            return -1;
        }
        const qint64 remaining = mBody.size() - mOffset;
        const qint64 amount = std::min(maximumSize, remaining);
        std::memcpy(data, mBody.constData() + mOffset,
                    static_cast<std::size_t>(amount));
        mOffset += static_cast<qsizetype>(amount);
        return amount;
    }

private:
    QByteArray mBody;
    qsizetype mOffset = 0;
};

class FakeNetworkAccessManager final : public QNetworkAccessManager {
public:
    std::vector<FakeHttpResponse> responses;
    std::vector<QNetworkRequest> requests;
    std::vector<QByteArray> requestBodies;
    std::vector<Operation> operations;

protected:
    auto createRequest(Operation operation, const QNetworkRequest &request,
                       QIODevice *outgoingData) -> QNetworkReply * override {
        operations.push_back(operation);
        requests.push_back(request);
        requestBodies.push_back(outgoingData ? outgoingData->readAll()
                                             : QByteArray {});
        const std::size_t index = requests.size() - 1;
        FakeHttpResponse response =
            index < responses.size()
                ? responses[index]
                : FakeHttpResponse {
                      .status = 500,
                      .body = QByteArrayLiteral("unexpected request"),
                      .error = QNetworkReply::UnknownServerError,
                  };
        return new FakeNetworkReply(request, std::move(response), this);
    }
};

auto fixture(QStringView name) -> QByteArray {
    QFile file(QStringLiteral(":/anime-land/tests/episode-provider/yhdmmm/") +
               name.toString());
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

auto copyResource(QStringView source, const QString &destination) -> bool {
    QFile input(source.toString());
    if (!input.open(QIODevice::ReadOnly)) {
        return false;
    }
    QFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) {
        return false;
    }
    return output.write(input.readAll()) >= 0;
}

auto lazyPlayable() -> OnlinePlayable {
    return {
        .stableKey = QStringLiteral("candidate|line|episode"),
        .displayName = QStringLiteral("示例动画 · 第01集 · 主线"),
        .match = {
            .stableKey = QStringLiteral("/4kvideo/42.html"),
            .title = QStringLiteral("示例动画"),
            .cover = {},
            .detail = {},
            .episodeTitle = QStringLiteral("第01集"),
            .sourceLine = QStringLiteral("主线"),
            .confidence = 1.0,
        },
        .assets = {{
            .kind = EpisodeAssetKind::Video,
            .streamType = MediaStreamType::Unknown,
            .displayName = QStringLiteral("主线"),
            .language = std::nullopt,
            .mimeType = std::nullopt,
            .data = {{QStringLiteral("continuation"),
                      QJsonObject {{QStringLiteral("playPath"),
                                    QStringLiteral("/4kplay/42-1-1.html")}}}},
        }},
        .expiresAt = std::nullopt,
    };
}

} // namespace

TEST(EpisodeProviderRegistry, RegistrationIsAtomicAndRejectsDuplicateKeys) {
    EpisodeProviderRegistry registry;
    auto first = std::make_shared<StubProvider>(QStringLiteral("org.test.one"));
    auto duplicate =
        std::make_shared<StubProvider>(QStringLiteral("org.test.one"));

    EXPECT_TRUE(registry.registerProvider(first));
    auto result = registry.registerProviders({
        std::make_shared<StubProvider>(QStringLiteral("org.test.two")),
        duplicate,
    });

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code,
              EpisodeProviderErrorCode::DuplicateProvider);
    EXPECT_EQ(registry.list().size(), 1U);
    EXPECT_EQ(registry.find(QStringLiteral("org.test.one")), first);
}

TEST(OnlinePlayableCache, UsesTtlAndProviderGenerationWithoutPersistence) {
    OnlinePlayableCache cache;
    const QDateTime now = QDateTime::fromMSecsSinceEpoch(
        1'700'000'000'000LL, QTimeZone::UTC);
    const OnlinePlayableCacheKey key {
        .providerKey = QStringLiteral("org.test.provider"),
        .providerGeneration = 3,
        .subjectId = SubjectId {10},
        .episodeId = EpisodeId {20},
        .normalizedSubjectName = QStringLiteral("示例动画"),
        .normalizedEpisodeName = QStringLiteral("第 1 集"),
    };

    auto stored = cache.store(key, {lazyPlayable()}, 5min, now);
    ASSERT_EQ(stored.size(), 1U);
    EXPECT_EQ(cache.size(), 1);
    EXPECT_TRUE(cache.find(key, now.addSecs(299)));
    EXPECT_FALSE(cache.find(key, now.addSecs(301)));
    EXPECT_EQ(cache.size(), 0);

    auto changedGeneration = key;
    changedGeneration.providerGeneration += 1;
    EXPECT_NE(OnlinePlayableCache::keyString(key),
              OnlinePlayableCache::keyString(changedGeneration));
}

TEST(EpisodeResourceService, ReusesSearchCacheWithoutCallingProviderAgain) {
    EpisodeProviderRegistry registry;
    OnlinePlayableCache cache;
    EpisodeResourceService service(registry, cache);
    auto provider =
        std::make_shared<StubProvider>(QStringLiteral("org.test.provider"));
    provider->searchResults.push_back(lazyPlayable());
    ASSERT_TRUE(registry.registerProvider(provider));
    const EpisodeQuery query {
        .subjectId = SubjectId {10},
        .episodeId = EpisodeId {20},
        .subjectName = QStringLiteral("示例动画"),
        .subjectAliases = {},
        .episodeName = QStringLiteral("第01集"),
        .episodeType = 0,
        .episodeNumber = 1.0,
    };

    auto first = service.search(provider->key(), query).wait();
    auto second = service.search(provider->key(), query).wait();

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_EQ(first->size(), 1U);
    ASSERT_EQ(second->size(), 1U);
    EXPECT_EQ(first->front().handle, second->front().handle);
    EXPECT_EQ(provider->searchCount, 1);
}

TEST(EpisodePluginScanner, LoadsValidPackagesAndIsolatesBrokenOnes) {
    initializeBuiltinEpisodeProviderResources();
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const QString providerDirectory =
        temporary.filePath(QStringLiteral("plugins/episode-providers"));
    const QString packageDirectory =
        QDir(providerDirectory).filePath(QStringLiteral("yhdmmm-copy"));
    const QString brokenDirectory =
        QDir(providerDirectory).filePath(QStringLiteral("broken"));
    ASSERT_TRUE(QDir().mkpath(packageDirectory));
    ASSERT_TRUE(QDir().mkpath(brokenDirectory));
    const QString resourceRoot = builtinYhdmmmPackageRoot();
    for (const QString &name : {QStringLiteral("manifest.json"),
                                QStringLiteral("config.defaults.json"),
                                QStringLiteral("config.schema.json"),
                                QStringLiteral("index.js")}) {
        ASSERT_TRUE(copyResource(QDir(resourceRoot).filePath(name),
                                 QDir(packageDirectory).filePath(name)));
    }
    QFile brokenManifest(QDir(brokenDirectory).filePath(
        QStringLiteral("manifest.json")));
    ASSERT_TRUE(brokenManifest.open(QIODevice::WriteOnly));
    ASSERT_GT(brokenManifest.write(QByteArrayLiteral("not-json")), 0);
    brokenManifest.close();

    auto scanned = scanEpisodeProviderPlugins(
        providerDirectory,
        temporary.filePath(QStringLiteral("config/providers")));

    ASSERT_EQ(scanned.plugins.size(), 1U);
    EXPECT_EQ(scanned.plugins.front().manifest.id,
              QStringLiteral("org.anime-land.yhdmmm"));
    ASSERT_EQ(scanned.issues.size(), 1U);
    EXPECT_TRUE(scanned.issues.front().packagePath.endsWith(
        QStringLiteral("broken")));
}

TEST(YhdmmmPlugin, LoadsWithoutNetworkAndResolvesFixtureM3u8Lazily) {
    initializeBuiltinEpisodeProviderResources();
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const QString overridePath = temporary.filePath(QStringLiteral("provider.json"));
    QFile overrideFile(overridePath);
    ASSERT_TRUE(overrideFile.open(QIODevice::WriteOnly));
    ASSERT_GT(overrideFile.write(QByteArrayLiteral(
                  R"({"minimumRequestIntervalMs":250,"maxCandidates":1})")),
              0);
    overrideFile.close();

    auto network = std::make_unique<FakeNetworkAccessManager>();
    auto *networkObserver = network.get();
    networkObserver->responses = {
        {.status = 200, .body = fixture(QStringLiteral("search.html"))},
        {.status = 200, .body = fixture(QStringLiteral("detail.html"))},
        {.status = 200, .body = fixture(QStringLiteral("play.html"))},
    };
    auto loaded = loadEpisodeProviderPlugin(
        builtinYhdmmmPackageRoot(), overridePath, std::move(network));

    ASSERT_TRUE(loaded) << loaded.error().message.toStdString();
    ASSERT_EQ(loaded->providers.size(), 1U);
    EXPECT_TRUE(networkObserver->requests.empty());
    EXPECT_EQ(loaded->providers.front()->key(),
              QStringLiteral("org.anime-land.yhdmmm.yhdmmm"));

    EpisodeQuery query {
        .subjectId = SubjectId {1},
        .episodeId = EpisodeId {2},
        .subjectName = QStringLiteral("示例动画第二季"),
        .subjectAliases = {},
        .episodeName = QStringLiteral("第01集"),
        .episodeNumber = 1.0,
    };
    EpisodeProviderRegistry registry;
    OnlinePlayableCache cache;
    EpisodeResourceService service(registry, cache);
    ASSERT_TRUE(registry.registerProviders(loaded->providers));
    auto searched = service.search(loaded->providers.front()->key(), query).wait();
    ASSERT_TRUE(searched) << searched.error().message.toStdString();
    ASSERT_EQ(searched->size(), 2U);
    EXPECT_FALSE(isResolved(searched->front().playable));
    ASSERT_EQ(networkObserver->requests.size(), 2U);
    EXPECT_EQ(networkObserver->operations[0],
              QNetworkAccessManager::PostOperation);
    EXPECT_EQ(networkObserver->requests[0].url().path(),
              QStringLiteral("/vodsearch.html"));
    EXPECT_TRUE(networkObserver->requestBodies[0].contains("wd="));
    EXPECT_EQ(networkObserver->requests[1].url().path(),
              QStringLiteral("/4kvideo/42.html"));

    auto resolved = service.resolve(searched->front().handle).wait();
    ASSERT_TRUE(resolved) << resolved.error().message.toStdString();
    ASSERT_TRUE(isResolved(*resolved));
    ASSERT_EQ(resolved->assets.size(), 1U);
    EXPECT_EQ(resolved->assets.front().streamType, MediaStreamType::Hls);
    EXPECT_EQ(resolved->assets.front().data.value(QStringLiteral("url")).toString(),
              QStringLiteral(
                  "https://fixture.ffzy-online5.com/anime/episode-01/index.m3u8"));
    ASSERT_EQ(networkObserver->requests.size(), 3U);
    EXPECT_EQ(networkObserver->requests[2].url().path(),
              QStringLiteral("/4kplay/42-1-1.html"));
}

#define EXPAND_IN_MAIN_WITH_ARGS(argc, argv)                                  \
    QCoreApplication qtApplication(argc, argv);                               \
    ilias::QIoContext ioContext;                                               \
    ioContext.install()
#include "common/common_main.hpp.in"
