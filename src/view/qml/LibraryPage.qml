import QtQuick
import QtQuick.Controls

Item {
    id: root

    signal openSubject(var subject)

    property string activeFilter: "全部"
    property string notice: ""
    property var visibleSubjects: FixtureData.subjects.filter(function(subject) {
        const query = searchField.text.trim().toLowerCase()
        const matchesText = query.length === 0
                || subject.title.toLowerCase().indexOf(query) >= 0
                || subject.subtitle.toLowerCase().indexOf(query) >= 0
        const matchesFilter = activeFilter === "全部"
                || (activeFilter === "在看" && subject.progress > 0 && subject.progress < 1)
                || (activeFilter === "未观看" && subject.progress === 0)
        return matchesText && matchesFilter
    })

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
            spacing: Theme.spacing

            PageHeader {
                width: parent.width
                title: "媒体库"
                subtitle: "浏览本地条目、观看进度和媒体关联状态。"
            }

            Row {
                width: parent.width
                spacing: 10

                TextField {
                    id: searchField
                    width: Math.min(360, parent.width - 360)
                    height: 40
                    placeholderText: "搜索本地媒体库"
                    color: Theme.text
                    placeholderTextColor: Theme.textFaint
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.accentText
                    font.pixelSize: Theme.bodySize
                    leftPadding: 13

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.surface
                        border.width: 1
                        border.color: searchField.activeFocus
                                      ? Theme.accent : Theme.border
                    }
                }

                Repeater {
                    model: ["全部", "在看", "未观看"]

                    AppButton {
                        text: modelData
                        primary: root.activeFilter === modelData
                        onClicked: root.activeFilter = modelData
                    }
                }

                Item { width: 8; height: 1 }

                AppButton {
                    text: "导入媒体"
                    primary: true
                    onClicked: root.notice = "Fixture：媒体扫描与导入尚未接入"
                }
            }

            AppText {
                width: parent.width
                height: root.notice.length > 0 ? implicitHeight : 0
                text: root.notice
                color: Theme.textMuted
                font.pixelSize: Theme.captionSize
                visible: root.notice.length > 0
            }

            SectionHeader {
                width: parent.width
                title: root.activeFilter
                detail: root.visibleSubjects.length + " 个条目"
            }

            Flow {
                width: parent.width
                height: childrenRect.height
                spacing: 16
                visible: root.visibleSubjects.length > 0

                Repeater {
                    model: root.visibleSubjects

                    SubjectCard {
                        subject: modelData
                        onActivated: selected => root.openSubject(selected)
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 150
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border
                visible: root.visibleSubjects.length === 0

                Column {
                    anchors.centerIn: parent
                    spacing: 8

                    AppText {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "没有匹配的条目"
                        color: Theme.text
                        font.pixelSize: Theme.bodySize
                    }

                    AppText {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "换个关键词或筛选条件试试"
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                    }
                }
            }

            Item { width: 1; height: 16 }
        }
    }
}
