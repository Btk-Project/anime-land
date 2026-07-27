#include "view/qml/qml_application.hpp"

#include "presentation/bangumi/calendar_view_model.hpp"
#include "presentation/bangumi/browser_view_model.hpp"
#include "presentation/library/library_view_model.hpp"
#include "presentation/library/subject_details_view_model.hpp"
#include "presentation/settings_view_model.hpp"

#include <QCoreApplication>
#include <QFont>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
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

auto runApplication(QGuiApplication &application,
                    BangumiCalendarViewModel *calendarViewModel,
                    BangumiBrowserViewModel *bangumiBrowserViewModel,
                    LibraryViewModel *libraryViewModel,
                    SubjectDetailsViewModel *subjectDetailsViewModel,
                    ApplicationSettingsViewModel *settingsViewModel,
                    bool fixtureMode) -> int {
    initializeAnimeLandQmlResources();
    QQuickStyle::setStyle(QStringLiteral("Basic"));
#if defined(_WIN32)
    auto applicationFont = application.font();
    applicationFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
    application.setFont(applicationFont);
#endif

    const bool smokeTest =
        qEnvironmentVariableIsSet("ANIME_LAND_UI_SMOKE_TEST");
    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlEngine::warnings,
        [](const QList<QQmlError> &warnings) {
            for (const auto &warning : warnings) {
                std::cerr << warning.toString().toStdString() << '\n';
            }
        });
    engine.rootContext()->setContextProperty(QStringLiteral("uiSmokeTest"),
                                             smokeTest);
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
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/AnimeLand/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return 3;
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
