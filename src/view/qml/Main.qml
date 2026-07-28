import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: root

    width: uiAssociationSmokeTest || uiCustomMetadataSmokeTest
           || uiSettingsSmokeTest || uiPaginationSmokeTest
           || uiLongMetadataSmokeTest ? 960 : 1320
    height: uiAssociationSmokeTest || uiCustomMetadataSmokeTest
            || uiSettingsSmokeTest || uiPaginationSmokeTest
            || uiLongMetadataSmokeTest ? 640 : 820
    minimumWidth: 960
    minimumHeight: 640
    visible: true
    title: "anime-land"
    color: Theme.background

    property int currentSection: 0
    property var selectedSubject: null
    property int smokeStep: 0
    property bool playerFullScreen: false
    property int visibilityBeforePlayerFullScreen: Window.Windowed

    palette.window: Theme.background
    palette.windowText: Theme.text
    palette.base: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.surface
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText
    palette.alternateBase: Theme.surfaceRaised
    palette.mid: Theme.border
    palette.light: Theme.surfaceRaised
    palette.dark: Theme.background
    palette.toolTipBase: Theme.surfaceRaised
    palette.toolTipText: Theme.text
    palette.placeholderText: Theme.textFaint

    Binding {
        target: Theme
        property: "preference"
        value: !uiFixtureMode && settingsViewModel
               ? settingsViewModel.themeMode : "system"
    }

    Binding {
        target: Theme
        property: "systemDark"
        value: !uiFixtureMode && settingsViewModel
               ? settingsViewModel.systemDark : initialSystemDark
    }

    function showRoot(section) {
        if (playerFullScreen)
            setPlayerFullScreen(false)
        currentSection = section
        pageStack.clear()
        if (section === 0)
            pageStack.push(homeComponent)
        else if (section === 1)
            pageStack.push(libraryComponent)
        else if (section === 2)
            pageStack.push(bangumiComponent)
        else
            pageStack.push(settingsComponent)
    }

    function openSubject(subject) {
        selectedSubject = subject
        pageStack.push(detailComponent, {"subject": subject})
    }

    function openContextLibrary() {
        pageStack.push(libraryComponent, {"showBackNavigation": true})
    }

    function openPlayer(subject) {
        selectedSubject = subject
        pageStack.push(playerComponent, {"subject": subject})
    }

    function setPlayerFullScreen(enabled) {
        if (playerFullScreen === enabled)
            return

        if (enabled) {
            visibilityBeforePlayerFullScreen = visibility
            playerFullScreen = true
            showFullScreen()
        }
        else {
            playerFullScreen = false
            if (visibilityBeforePlayerFullScreen === Window.Maximized)
                showMaximized()
            else
                showNormal()
        }
    }

    Connections {
        target: playbackController
        enabled: !uiFixtureMode && playbackController

        function onOpenRequested(title) {
            const currentPage = pageStack.currentItem
            if (currentPage && currentPage.objectName === "playerPage")
                return

            const episodes = currentPage
                    && currentPage.objectName === "subjectDetailPage"
                    ? currentPage.episodeModel : []
            root.openPlayer({
                "title": title,
                "episode": "本地媒体",
                "episodes": episodes
            })
        }
    }

    function goBack() {
        if (pageStack.depth > 1)
            pageStack.pop()
        else
            showRoot(currentSection)
    }

    Shortcut {
        sequence: "Alt+Left"
        enabled: pageStack.depth > 1
        onActivated: root.goBack()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: appSidebar
            Layout.preferredWidth: visible ? 196 : 0
            Layout.fillHeight: true
            visible: !root.playerFullScreen
            color: Theme.sidebar
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                anchors.topMargin: 20
                anchors.bottomMargin: 16
                spacing: 8

                Column {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 22
                    spacing: 4

                    AppText {
                        text: "anime-land"
                        color: Theme.text
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                    }

                    AppText {
                        text: "本地动画媒体库"
                        color: Theme.textFaint
                        font.pixelSize: Theme.captionSize
                    }
                }

                Repeater {
                    model: [
                        {"label": "首页", "marker": "H"},
                        {"label": "媒体库", "marker": "L"},
                        {"label": "Bangumi", "marker": "B"}
                    ]

                    NavigationItem {
                        Layout.fillWidth: true
                        text: modelData.label
                        marker: modelData.marker
                        selected: root.currentSection === index
                                  && pageStack.depth === 1
                        onClicked: root.showRoot(index)
                    }
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 54
                    radius: Theme.radiusSmall
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border

                    Column {
                        anchors.centerIn: parent
                        spacing: 3

                        AppText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: uiFixtureMode ? "UI Fixture" : "Live Calendar"
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            font.weight: Font.DemiBold
                        }

                        AppText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: uiFixtureMode
                                  ? "独立 View 调试模式"
                                  : "真实搜索、收藏与媒体库"
                            color: Theme.textFaint
                            font.pixelSize: 10
                        }
                    }
                }

                NavigationItem {
                    Layout.fillWidth: true
                    text: "设置"
                    marker: "S"
                    selected: root.currentSection === 3
                              && pageStack.depth === 1
                    onClicked: root.showRoot(3)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.background

            StackView {
                id: pageStack
                anchors.fill: parent

                pushEnter: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 }
                }
                pushExit: Transition {
                    NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 }
                }
                popEnter: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 }
                }
                popExit: Transition {
                    NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 }
                }
            }
        }
    }

    Component {
        id: homeComponent
        HomePage {
            onOpenSubject: subject => root.openSubject(subject)
            onCalendarRequested: root.showRoot(2)
            onLibraryRequested: root.showRoot(1)
        }
    }

    Component {
        id: libraryComponent
        LibraryPage {
            onOpenSubject: subject => root.openSubject(subject)
            onBackRequested: root.goBack()
        }
    }

    Component {
        id: bangumiComponent
        BangumiPage {
            onOpenSubject: subject => root.openSubject(subject)
        }
    }

    Component {
        id: settingsComponent
        SettingsPage {
            onLibraryRequested: root.showRoot(1)
        }
    }

    Component {
        id: detailComponent
        SubjectDetailPage {
            onBackRequested: root.goBack()
            onPlayRequested: subject => root.openPlayer(subject)
            onLibraryRequested: root.openContextLibrary()
        }
    }

    Component {
        id: playerComponent
        PlayerPage {
            fullScreen: root.playerFullScreen
            onBackRequested: root.goBack()
            onFullScreenRequested: enabled =>
                root.setPlayerFullScreen(enabled)
            onPlayEpisodeRequested: episode => {
                if (!uiFixtureMode && subjectDetailsViewModel
                        && episode.linked)
                    subjectDetailsViewModel.playEpisode(episode.id)
            }
        }
    }

    Component.onCompleted: {
        root.showRoot(uiSettingsSmokeTest ? 3
                      : uiAssociationSmokeTest || uiCustomMetadataSmokeTest
                        ? 1 : 0)
        if (uiPaginationSmokeTest || uiLongMetadataSmokeTest)
            Qt.callLater(function() {
                root.openSubject(FixtureData.subjects[0])
            })
        if (!uiFixtureMode)
            calendarViewModel.refresh()
        if (!uiFixtureMode && libraryViewModel)
            libraryViewModel.refresh()
        if (!uiFixtureMode && bangumiBrowserViewModel)
            bangumiBrowserViewModel.restoreSession()
    }

    Timer {
        interval: 60
        running: uiSmokeTest && !uiAssociationSmokeTest
                 && !uiCustomMetadataSmokeTest && !uiSettingsSmokeTest
                 && !uiPaginationSmokeTest && !uiLongMetadataSmokeTest
        repeat: true

        onTriggered: {
            root.smokeStep += 1
            if (root.smokeStep === 1)
                root.showRoot(1)
            else if (root.smokeStep === 2)
                root.showRoot(2)
            else if (root.smokeStep === 3)
                root.openSubject(FixtureData.subjects[0])
            else if (root.smokeStep === 4)
                root.openPlayer(FixtureData.subjects[0])
            else if (root.smokeStep === 5)
                root.showRoot(3)
            else {
                stop()
                Qt.quit()
            }
        }
    }
}
