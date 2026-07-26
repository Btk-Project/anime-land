#pragma once

class QGuiApplication;

namespace anime_land::qml {

/** Runs the fixture-backed Qt Quick application shell. */
auto runApplication(QGuiApplication &application) -> int;

} // namespace anime_land::qml
