#include "view/qml/qml_application.hpp"
#include "view/qml/image_network_access_manager.hpp"
#include "view/playback/playback_video_surface.hpp"
#include "view/playback/video_output_item.hpp"

#include "presentation/bangumi/calendar_view_model.hpp"
#include "presentation/bangumi/browser_view_model.hpp"
#include "presentation/library/library_view_model.hpp"
#include "presentation/library/subject_details_view_model.hpp"
#include "presentation/settings_view_model.hpp"
#include "presentation/playback/playback_controller.hpp"
#include "model/bangumi/network_cache.hpp"

#include <QCoreApplication>
#include <QFont>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <qqml.h>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QPalette>
#include <QTimer>
#include <QUrl>

#include <iostream>

void initializeAnimeLandQmlResources() {
    Q_INIT_RESOURCE(anime_land_qml);
}

namespace anime_land::qml {

namespace {

class QmlNetworkAccessManagerFactory final
    : public QQmlNetworkAccessManagerFactory {
public:
    explicit QmlNetworkAccessManagerFactory(
        BangumiNetworkCacheOptions options)
        : mOptions(std::move(options)) {}

    auto create(QObject *parent) -> QNetworkAccessManager * override {
        auto *network = new ImageNetworkAccessManager(parent);
        installBangumiNetworkCache(*network, mOptions,
                                   NetworkCachePartition::Images);
        return network;
    }

private:
    BangumiNetworkCacheOptions mOptions;
};

} // namespace

auto runApplication(QGuiApplication &application,
                    BangumiCalendarViewModel *calendarViewModel,
                    BangumiBrowserViewModel *bangumiBrowserViewModel,
                    LibraryViewModel *libraryViewModel,
                    SubjectDetailsViewModel *subjectDetailsViewModel,
                    ApplicationSettingsViewModel *settingsViewModel,
                    PlaybackController *playbackController,
                    PlaybackVideoSurface *playbackVideoSurface,
                    bool fixtureMode,
                    const BangumiNetworkCacheOptions &cacheOptions) -> int {
    initializeAnimeLandQmlResources();
    qmlRegisterType<VideoOutputItem>("AnimeLand.Playback", 1, 0,
                                     "VideoOutputItem");
    QQuickStyle::setStyle(QStringLiteral("Basic"));
#if defined(_WIN32)
    auto applicationFont = application.font();
    applicationFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
    application.setFont(applicationFont);
#endif

    const bool smokeTest =
        qEnvironmentVariableIsSet("ANIME_LAND_UI_SMOKE_TEST");
    const bool associationSmokeTest = qEnvironmentVariableIsSet(
        "ANIME_LAND_UI_ASSOCIATION_SMOKE_TEST");
    const bool customMetadataSmokeTest = qEnvironmentVariableIsSet(
        "ANIME_LAND_UI_CUSTOM_METADATA_SMOKE_TEST");
    const bool settingsSmokeTest = qEnvironmentVariableIsSet(
        "ANIME_LAND_UI_SETTINGS_SMOKE_TEST");
    const bool paginationSmokeTest = qEnvironmentVariableIsSet(
        "ANIME_LAND_UI_PAGINATION_SMOKE_TEST");
    const bool longMetadataSmokeTest = qEnvironmentVariableIsSet(
        "ANIME_LAND_UI_LONG_METADATA_SMOKE_TEST");
    QQmlApplicationEngine engine;
    QmlNetworkAccessManagerFactory networkFactory(cacheOptions);
    engine.setNetworkAccessManagerFactory(&networkFactory);
    QObject::connect(
        &engine, &QQmlEngine::warnings,
        [](const QList<QQmlError> &warnings) {
            for (const auto &warning : warnings) {
                std::cerr << warning.toString().toStdString() << '\n';
            }
        });
    engine.rootContext()->setContextProperty(QStringLiteral("uiSmokeTest"),
                                             smokeTest);
    engine.rootContext()->setContextProperty(
        QStringLiteral("uiAssociationSmokeTest"), associationSmokeTest);
    engine.rootContext()->setContextProperty(
        QStringLiteral("uiCustomMetadataSmokeTest"),
        customMetadataSmokeTest);
    engine.rootContext()->setContextProperty(
        QStringLiteral("uiSettingsSmokeTest"), settingsSmokeTest);
    engine.rootContext()->setContextProperty(
        QStringLiteral("uiPaginationSmokeTest"), paginationSmokeTest);
    engine.rootContext()->setContextProperty(
        QStringLiteral("uiLongMetadataSmokeTest"), longMetadataSmokeTest);
    engine.rootContext()->setContextProperty(QStringLiteral("uiFixtureMode"),
                                             fixtureMode);
    engine.rootContext()->setContextProperty(
        QStringLiteral("initialSystemDark"),
        application.palette().color(QPalette::Window).lightness() < 128);
    engine.rootContext()->setContextProperty(
        QStringLiteral("calendarViewModel"), calendarViewModel);
    engine.rootContext()->setContextProperty(
        QStringLiteral("bangumiBrowserViewModel"),
        bangumiBrowserViewModel);
    engine.rootContext()->setContextProperty(
        QStringLiteral("libraryViewModel"), libraryViewModel);
    engine.rootContext()->setContextProperty(
        QStringLiteral("subjectDetailsViewModel"),
        subjectDetailsViewModel);
    engine.rootContext()->setContextProperty(
        QStringLiteral("settingsViewModel"), settingsViewModel);
    engine.rootContext()->setContextProperty(
        QStringLiteral("playbackController"), playbackController);
    engine.rootContext()->setContextProperty(
        QStringLiteral("playbackVideoSurface"), playbackVideoSurface);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/AnimeLand/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return 3;
    }

    const QString playbackSmokeFile =
        qEnvironmentVariable("ANIME_LAND_PLAYBACK_SMOKE_FILE");
    if (playbackController != nullptr && !playbackSmokeFile.isEmpty()) {
        QTimer::singleShot(
            0, playbackController,
            [playbackController, playbackSmokeFile] {
                playbackController->openMedia(
                    QUrl::fromLocalFile(playbackSmokeFile));
            });
    }

    if (smokeTest) {
        // Fallback guard in case the QML smoke sequence cannot reach Qt.quit().
        QTimer::singleShot(2000, &application, &QCoreApplication::quit);
    }
    const QString screenshotPath =
        qEnvironmentVariable("ANIME_LAND_UI_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(1500, &application,
                           [&application, &engine, screenshotPath]() {
            auto *window =
                qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
            if (window == nullptr
                || !window->grabWindow().save(screenshotPath)) {
                qWarning("Failed to save UI screenshot");
            }
            application.quit();
        });
    }
    return application.exec();
}

} // namespace anime_land::qml
