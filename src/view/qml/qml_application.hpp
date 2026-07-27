#pragma once

class QGuiApplication;

namespace anime_land {
class BangumiCalendarViewModel;
class BangumiBrowserViewModel;
class LibraryViewModel;
class SubjectDetailsViewModel;
class ApplicationSettingsViewModel;
}

namespace anime_land::qml {

/** Runs the Qt Quick shell with either live Presentation state or fixtures. */
auto runApplication(QGuiApplication &application,
                    BangumiCalendarViewModel *calendarViewModel,
                    BangumiBrowserViewModel *bangumiBrowserViewModel,
                    LibraryViewModel *libraryViewModel,
                    SubjectDetailsViewModel *subjectDetailsViewModel,
                    ApplicationSettingsViewModel *settingsViewModel,
                    bool fixtureMode) -> int;

} // namespace anime_land::qml
