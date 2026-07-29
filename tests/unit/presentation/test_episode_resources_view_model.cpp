#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>

#include <ilias/platform/qt.hpp>

#include "presentation/episode_resource/episode_resources_view_model.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

using namespace anime_land;

namespace {

class StubProvider final : public EpisodeProvider {
public:
    StubProvider(QString key, QString name)
        : mKey(std::move(key)), mName(std::move(name)) {}

    auto key() const -> QString override { return mKey; }
    auto name() const -> QString override { return mName; }
    auto icon() const -> QUrl override { return {}; }
    auto generation() const -> std::uint64_t override { return 1; }
    auto ping() -> ilias::Task<EpisodeProviderResult<ProviderHealth>> override {
        co_return ProviderHealth {
            .reachable = true,
            .mirrorId = {},
            .detail = {},
            .latencyMilliseconds = 0,
        };
    }
    auto search(EpisodeQuery query)
        -> ilias::Task<
            EpisodeProviderResult<std::vector<OnlinePlayable>>> override {
        ++searchCount;
        lastQuery = std::move(query);
        if (searchError) {
            co_return ilias::Err(*searchError);
        }
        co_return searchResults;
    }
    auto resolve(OnlinePlayable playable)
        -> ilias::Task<EpisodeProviderResult<OnlinePlayable>> override {
        ++resolveCount;
        for (auto &asset : playable.assets) {
            if (asset.kind == EpisodeAssetKind::Video) {
                asset.streamType = MediaStreamType::Hls;
                asset.mimeType = QStringLiteral(
                    "application/vnd.apple.mpegurl");
                asset.data.insert(
                    QStringLiteral("url"),
                    QStringLiteral("https://media.example.test/episode.m3u8"));
            }
        }
        co_return playable;
    }
    void cancel() override { ++cancelCount; }

    int searchCount = 0;
    int resolveCount = 0;
    int cancelCount = 0;
    EpisodeQuery lastQuery;
    std::vector<OnlinePlayable> searchResults;
    std::optional<EpisodeProviderError> searchError;

private:
    QString mKey;
    QString mName;
};

auto queryForEpisode(EpisodeId episode) -> EpisodeQuery {
    return {
        .subjectId = SubjectId {21},
        .episodeId = episode,
        .subjectName = QStringLiteral("葬送的芙莉莲"),
        .subjectAliases = {QStringLiteral("Sousou no Frieren")},
        .episodeName = QStringLiteral("冒险的终点"),
        .episodeType = 0,
        .episodeNumber = 1.0,
    };
}

auto fuzzyPlayable() -> OnlinePlayable {
    return {
        .stableKey = QStringLiteral("frieren|line-a|ep-1"),
        .displayName = QStringLiteral("葬送的芙莉莲 · 第 1 集 · 线路 A"),
        .match = {
            .stableKey = QStringLiteral("remote-frieren"),
            .title = QStringLiteral("葬送的芙莉莲 第一季"),
            .cover = QUrl(QStringLiteral("https://img.example.test/f.jpg")),
            .detail = QStringLiteral("奇幻 / 冒险"),
            .episodeTitle = QStringLiteral("第01集"),
            .sourceLine = QStringLiteral("线路 A"),
            .confidence = 0.75,
        },
        .assets = {{
            .kind = EpisodeAssetKind::Video,
            .streamType = MediaStreamType::Unknown,
            .displayName = QStringLiteral("线路 A"),
            .language = std::nullopt,
            .mimeType = std::nullopt,
            .data = {{QStringLiteral("continuation"),
                      QJsonObject {{QStringLiteral("playPath"),
                                    QStringLiteral("/play/1")}}}},
        }},
        .expiresAt = std::nullopt,
    };
}

void processUntilSettled(EpisodeResourcesViewModel &viewModel) {
    QElapsedTimer timer;
    timer.start();
    while (viewModel.busy() && timer.elapsed() < 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

auto providerByKey(const QVariantList &providers, QStringView key)
    -> QVariantMap {
    for (const auto &value : providers) {
        const auto provider = value.toMap();
        if (provider.value(QStringLiteral("key")).toString() == key) {
            return provider;
        }
    }
    return {};
}

} // namespace

TEST(EpisodeResourcesViewModel,
     SearchesOnlySelectedProvidersAndMapsSafeSuggestionData) {
    EpisodeProviderRegistry registry;
    OnlinePlayableCache cache;
    EpisodeResourceService service(registry, cache);
    auto selected = std::make_shared<StubProvider>(
        QStringLiteral("org.test.selected"), QStringLiteral("Selected"));
    auto skipped = std::make_shared<StubProvider>(
        QStringLiteral("org.test.skipped"), QStringLiteral("Skipped"));
    selected->searchResults = {fuzzyPlayable()};
    ASSERT_TRUE(registry.registerProviders({selected, skipped}));

    EpisodeResourcesViewModel viewModel(
        service, registry,
        [](EpisodeId episode)
            -> ilias::Task<EpisodeProviderResult<EpisodeQuery>> {
            co_return queryForEpisode(episode);
        });
    viewModel.openEpisode(31);
    processUntilSettled(viewModel);
    ASSERT_TRUE(viewModel.episodeReady());
    EXPECT_EQ(viewModel.episode().value(QStringLiteral("number")).toString(),
              QStringLiteral("EP1"));

    viewModel.setProviderSelected(skipped->key(), false);
    EXPECT_EQ(viewModel.selectedProviderCount(), 1);
    viewModel.searchSelectedProviders();
    processUntilSettled(viewModel);

    EXPECT_EQ(selected->searchCount, 1);
    EXPECT_EQ(skipped->searchCount, 0);
    EXPECT_EQ(selected->lastQuery.episodeId, EpisodeId {31});
    const auto provider = providerByKey(viewModel.providers(), selected->key());
    EXPECT_EQ(provider.value(QStringLiteral("status")).toString(),
              QStringLiteral("ready"));
    const auto results = provider.value(QStringLiteral("results")).toList();
    ASSERT_EQ(results.size(), 1);
    const auto result = results.front().toMap();
    EXPECT_EQ(result.value(QStringLiteral("subjectTitle")).toString(),
              QStringLiteral("葬送的芙莉莲 第一季"));
    EXPECT_EQ(result.value(QStringLiteral("episodeTitle")).toString(),
              QStringLiteral("第01集"));
    EXPECT_EQ(result.value(QStringLiteral("matchLabel")).toString(),
              QStringLiteral("可能匹配"));
    EXPECT_TRUE(result.value(QStringLiteral("temporary")).toBool());
    EXPECT_FALSE(result.contains(QStringLiteral("data")));
    EXPECT_FALSE(result.contains(QStringLiteral("url")));
    const auto asset = result.value(QStringLiteral("assets"))
                           .toList()
                           .front()
                           .toMap();
    EXPECT_FALSE(asset.contains(QStringLiteral("data")));
}

TEST(EpisodeResourcesViewModel, KeepsPerProviderEmptyAndErrorStates) {
    EpisodeProviderRegistry registry;
    OnlinePlayableCache cache;
    EpisodeResourceService service(registry, cache);
    auto empty = std::make_shared<StubProvider>(
        QStringLiteral("org.test.empty"), QStringLiteral("Empty"));
    auto failed = std::make_shared<StubProvider>(
        QStringLiteral("org.test.failed"), QStringLiteral("Failed"));
    failed->searchError = episodeProviderError(
        EpisodeProviderErrorCode::NetworkError,
        QStringLiteral("测试网络不可用"), true);
    ASSERT_TRUE(registry.registerProviders({empty, failed}));

    EpisodeResourcesViewModel viewModel(
        service, registry,
        [](EpisodeId episode)
            -> ilias::Task<EpisodeProviderResult<EpisodeQuery>> {
            co_return queryForEpisode(episode);
        });
    viewModel.openEpisode(31);
    processUntilSettled(viewModel);
    viewModel.searchSelectedProviders();
    processUntilSettled(viewModel);

    const auto providers = viewModel.providers();
    EXPECT_EQ(providerByKey(providers, empty->key())
                  .value(QStringLiteral("status"))
                  .toString(),
              QStringLiteral("empty"));
    const auto failedState = providerByKey(providers, failed->key());
    EXPECT_EQ(failedState.value(QStringLiteral("status")).toString(),
              QStringLiteral("error"));
    EXPECT_TRUE(failedState.value(QStringLiteral("message"))
                    .toString()
                    .contains(QStringLiteral("测试网络不可用")));
}

TEST(EpisodeResourcesViewModel, ResolvesOpaqueHandleBeforeLaunchingPlayback) {
    EpisodeProviderRegistry registry;
    OnlinePlayableCache cache;
    EpisodeResourceService service(registry, cache);
    auto provider = std::make_shared<StubProvider>(
        QStringLiteral("org.test.play"), QStringLiteral("Play"));
    provider->searchResults = {fuzzyPlayable()};
    ASSERT_TRUE(registry.registerProvider(provider));
    QUrl launchedUrl;
    QString launchedTitle;
    bool playbackOpened = false;

    EpisodeResourcesViewModel viewModel(
        service, registry,
        [](EpisodeId episode)
            -> ilias::Task<EpisodeProviderResult<EpisodeQuery>> {
            co_return queryForEpisode(episode);
        },
        [&](const QUrl &url, const QString &title) {
            launchedUrl = url;
            launchedTitle = title;
            return true;
        });
    QObject::connect(&viewModel,
                     &EpisodeResourcesViewModel::playbackOpened,
                     [&] { playbackOpened = true; });
    viewModel.openEpisode(31);
    processUntilSettled(viewModel);
    viewModel.searchSelectedProviders();
    processUntilSettled(viewModel);
    const auto result = providerByKey(viewModel.providers(), provider->key())
                            .value(QStringLiteral("results"))
                            .toList()
                            .front()
                            .toMap();

    viewModel.playOnline(result.value(QStringLiteral("handle")).toString());
    processUntilSettled(viewModel);

    EXPECT_EQ(provider->resolveCount, 1);
    EXPECT_EQ(launchedUrl,
              QUrl(QStringLiteral(
                  "https://media.example.test/episode.m3u8")));
    EXPECT_EQ(launchedTitle,
              QStringLiteral("葬送的芙莉莲 · 第 1 集 · 线路 A"));
    EXPECT_TRUE(playbackOpened);
    EXPECT_TRUE(viewModel.errorMessage().isEmpty());
}

#define EXPAND_IN_MAIN_WITH_ARGS(argc, argv)                                  \
    QCoreApplication qtApplication(argc, argv);                              \
    ilias::QIoContext ioContext;                                              \
    ioContext.install()
#include "common/common_main.hpp.in"
