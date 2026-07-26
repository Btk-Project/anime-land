import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    width: 1320
    height: 820
    minimumWidth: 960
    minimumHeight: 640
    visible: true
    title: "anime-land"
    color: Theme.background

    property int currentSection: 0
    property var selectedSubject: FixtureData.subjects[0]
    property int smokeStep: 0

    palette.window: Theme.background
    palette.windowText: Theme.text
    palette.base: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.surface
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText

    function showRoot(section) {
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
        if (!uiFixtureMode && subjectDetailsViewModel) {
            if (subject && subject.subjectId > 0)
                subjectDetailsViewModel.openSubject(subject.subjectId)
            else if (subject && subject.bangumiId > 0)
                subjectDetailsViewModel.openBangumiSubject(subject.bangumiId)
            else
                subjectDetailsViewModel.clear()
        }
        pageStack.push(detailComponent, {"subject": subject})
    }

    function openPlayer(subject) {
        selectedSubject = subject
        pageStack.push(playerComponent, {"subject": subject})
    }

    function goBack() {
        if (pageStack.depth > 1)
            pageStack.pop()
        else
            showRoot(currentSection)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 196
            Layout.fillHeight: true
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
                                  : "搜索与收藏仍为 Fixture"
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
            onPlayRequested: subject => root.openPlayer(subject)
            onCalendarRequested: root.showRoot(2)
        }
    }

    Component {
        id: libraryComponent
        LibraryPage {
            onOpenSubject: subject => root.openSubject(subject)
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
        SettingsPage {}
    }

    Component {
        id: detailComponent
        SubjectDetailPage {
            onBackRequested: root.goBack()
            onPlayRequested: subject => root.openPlayer(subject)
            onLibraryRequested: root.showRoot(1)
        }
    }

    Component {
        id: playerComponent
        PlayerPage {
            onBackRequested: root.goBack()
        }
    }

    Component.onCompleted: {
        root.showRoot(0)
        if (!uiFixtureMode)
            calendarViewModel.refresh()
    }

    Timer {
        interval: 60
        running: uiSmokeTest
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
