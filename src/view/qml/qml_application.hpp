#pragma once

class QGuiApplication;

namespace anime_land {
struct BangumiNetworkCacheOptions;
class BangumiCalendarViewModel;
class BangumiBrowserViewModel;
class LibraryViewModel;
class SubjectDetailsViewModel;
class EpisodeResourcesViewModel;
class ApplicationSettingsViewModel;
class PlaybackController;
}

namespace anime_land::qml {
class PlaybackVideoSurface;
}

namespace anime_land::qml {

/** Runs the Qt Quick shell with either live Presentation state or fixtures. */
auto runApplication(QGuiApplication &application,
                    BangumiCalendarViewModel *calendarViewModel,
                    BangumiBrowserViewModel *bangumiBrowserViewModel,
                    LibraryViewModel *libraryViewModel,
                    SubjectDetailsViewModel *subjectDetailsViewModel,
                    EpisodeResourcesViewModel *episodeResourcesViewModel,
                    ApplicationSettingsViewModel *settingsViewModel,
                    PlaybackController *playbackController,
                    PlaybackVideoSurface *playbackVideoSurface,
                    bool fixtureMode,
                    const BangumiNetworkCacheOptions &cacheOptions) -> int;

} // namespace anime_land::qml
