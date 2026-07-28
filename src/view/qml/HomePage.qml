import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    signal openSubject(var subject)
    signal calendarRequested()
    signal libraryRequested()

    readonly property var todayItems:
        !uiFixtureMode && calendarViewModel
        ? calendarViewModel.todayItems.slice(0, 3) : []
    readonly property int todayItemCount:
        !uiFixtureMode && calendarViewModel
        ? calendarViewModel.todayItemCount : 0
    readonly property string todayLabel:
        !uiFixtureMode && calendarViewModel
        ? calendarViewModel.todayLabel : ""
    readonly property bool calendarLoading:
        !uiFixtureMode && calendarViewModel && calendarViewModel.loading
    readonly property string calendarError:
        !uiFixtureMode && calendarViewModel
        ? calendarViewModel.errorMessage : ""
    readonly property var featured:
        todayItems.length > 0 ? todayItems[0] : null
    readonly property var librarySubjects:
        !uiFixtureMode && libraryViewModel
        ? libraryViewModel.subjectGroups : []
    readonly property bool libraryLoading:
        !uiFixtureMode && libraryViewModel && libraryViewModel.loading
    readonly property string libraryError:
        !uiFixtureMode && libraryViewModel
        ? libraryViewModel.errorMessage : ""

    Flickable {
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: content.implicitHeight + Theme.pageMargin
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: content
            x: Theme.pageMargin
            width: parent.width - Theme.pageMargin * 2
            spacing: Theme.spacingLarge

            PageHeader {
                width: parent.width
                title: "首页"
                subtitle: "查看今日放送与已经关联的本地媒体。"
            }

            Rectangle {
                width: parent.width
                height: visible ? 226 : 0
                visible: root.featured !== null
                radius: Theme.radiusLarge
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 28

                    Column {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 10

                        AppText {
                            text: "今日焦点"
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            font.weight: Font.DemiBold
                        }

                        AppText {
                            width: parent.width
                            text: root.featured ? root.featured.title : ""
                            color: Theme.text
                            font.pixelSize: 28
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        AppText {
                            width: parent.width
                            text: root.featured
                                  ? (root.featured.subtitle
                                     || root.featured.meta || "") : ""
                            color: Theme.textMuted
                            font.pixelSize: Theme.bodySize
                            elide: Text.ElideRight
                        }

                        AppText {
                            width: parent.width
                            text: root.featured
                                  ? (root.featured.summary || "暂无简介") : ""
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            wrapMode: Text.WordWrap
                        }

                        Row {
                            spacing: 10

                            AppButton {
                                text: "查看详情"
                                primary: true
                                onClicked: root.openSubject(root.featured)
                            }

                            AppButton {
                                text: "完整放送表"
                                onClicked: root.calendarRequested()
                            }
                        }
                    }

                    CoverImage {
                        Layout.preferredWidth: 126
                        Layout.fillHeight: true
                        source: root.featured
                                ? (root.featured.coverUrl || "") : ""
                        title: root.featured ? root.featured.title : ""
                        fallbackColor: root.featured
                                       ? (root.featured.color
                                          || Theme.surfaceRaised)
                                       : Theme.surfaceRaised
                    }
                }
            }

            SectionHeader {
                width: parent.width
                title: "今日放送"
                detail: root.calendarLoading
                    ? "正在加载"
                    : root.calendarError.length > 0
                      ? "加载失败"
                      : root.todayLabel.length > 0
                        ? root.todayLabel + " · " + root.todayItemCount + " 部"
                        : "尚未连接数据源"
            }

            Rectangle {
                width: parent.width
                height: 112
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 12

                    Repeater {
                        model: root.todayItems

                        Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: Theme.radiusSmall
                            color: todayMouse.containsMouse
                                   ? Theme.surfaceHover : Theme.surfaceRaised

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 10

                                CoverImage {
                                    Layout.preferredWidth: 52
                                    Layout.fillHeight: true
                                    source: modelData.coverUrl || ""
                                    title: modelData.title || ""
                                    fallbackColor: modelData.color
                                                   || Theme.surfaceRaised
                                }

                                Column {
                                    Layout.fillWidth: true
                                    spacing: 5

                                    AppText {
                                        width: parent.width
                                        text: modelData.title
                                        color: Theme.text
                                        font.pixelSize: Theme.bodySize
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    AppText {
                                        width: parent.width
                                        text: modelData.meta
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.captionSize
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            MouseArea {
                                id: todayMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.openSubject(modelData)
                            }
                        }
                    }

                    AppText {
                        visible: root.todayItems.length === 0
                        Layout.fillWidth: true
                        text: root.calendarLoading
                            ? "正在从 Bangumi 加载今日放送……"
                            : root.calendarError.length > 0
                              ? root.calendarError
                              : "今日暂无放送条目。"
                        color: root.calendarError.length > 0
                               ? Theme.danger : Theme.textMuted
                        font.pixelSize: Theme.bodySize
                        wrapMode: Text.Wrap
                    }

                    AppButton {
                        Layout.preferredWidth: 112
                        text: "完整放送表"
                        primary: true
                        onClicked: root.calendarRequested()
                    }
                }
            }

            SectionHeader {
                width: parent.width
                title: "本地媒体库"
                detail: root.libraryLoading
                        ? "正在读取"
                        : root.libraryError.length > 0
                          ? "读取失败"
                          : root.librarySubjects.length + " 个已关联条目"
            }

            Flow {
                width: parent.width
                height: visible ? childrenRect.height : 0
                visible: root.librarySubjects.length > 0
                spacing: 16

                Repeater {
                    model: root.librarySubjects.slice(0, 6)

                    SubjectCard {
                        subject: modelData
                        onActivated: selected => root.openSubject(selected)
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: visible ? 118 : 0
                visible: root.librarySubjects.length === 0
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 16

                    Column {
                        Layout.fillWidth: true
                        spacing: 7

                        AppText {
                            width: parent.width
                            text: root.libraryLoading
                                  ? "正在读取本地媒体库……"
                                  : root.libraryError.length > 0
                                    ? root.libraryError
                                    : "还没有已关联的本地条目"
                            color: root.libraryError.length > 0
                                   ? Theme.danger : Theme.text
                            font.pixelSize: Theme.bodySize
                            font.weight: Font.DemiBold
                            wrapMode: Text.Wrap
                        }

                        AppText {
                            width: parent.width
                            visible: !root.libraryLoading
                                     && root.libraryError.length === 0
                            text: "导入媒体并关联 Bangumi 条目后，会在这里显示。"
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                        }
                    }

                    AppButton {
                        visible: !root.libraryLoading
                        text: "打开媒体库"
                        primary: true
                        onClicked: root.libraryRequested()
                    }
                }
            }

            Item { width: 1; height: 8 }
        }
    }
}
