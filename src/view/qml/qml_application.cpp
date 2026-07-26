#include "view/qml/qml_application.hpp"

#include <QCoreApplication>
#include <QFont>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <iostream>

void initializeAnimeLandQmlResources() {
    Q_INIT_RESOURCE(anime_land_qml);
}

namespace anime_land::qml {

auto runApplication(QGuiApplication &application) -> int {
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
        QTimer::singleShot(500, &application,
                           [&application, &engine, screenshotPath]() {
            auto *window =
                qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
            if (window == nullptr
                || !window->grabWindow().save(screenshotPath)) {
                qWarning("Failed to save fixture UI screenshot");
            }
            application.quit();
        });
    }
    return application.exec();
}

} // namespace anime_land::qml
