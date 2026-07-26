#pragma once

class QGuiApplication;

namespace anime_land {
class BangumiCalendarViewModel;
class LibraryViewModel;
class SubjectDetailsViewModel;
}

namespace anime_land::qml {

/** Runs the Qt Quick shell with either live Presentation state or fixtures. */
auto runApplication(QGuiApplication &application,
                    BangumiCalendarViewModel *calendarViewModel,
                    LibraryViewModel *libraryViewModel,
                    SubjectDetailsViewModel *subjectDetailsViewModel,
                    bool fixtureMode) -> int;

} // namespace anime_land::qml
