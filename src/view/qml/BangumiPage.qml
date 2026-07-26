import QtQuick
import QtQuick.Controls

Item {
    id: root

    signal openSubject(var subject)

    property int currentTab: 0
    property var visibleSubjects: {
        const base = currentTab === 0
                ? FixtureData.bangumiSubjects : FixtureData.subjects.slice(0, 4)
        const query = searchField.text.trim().toLowerCase()
        if (query.length === 0)
            return base
        return base.filter(function(subject) {
            return subject.title.toLowerCase().indexOf(query) >= 0
                    || subject.subtitle.toLowerCase().indexOf(query) >= 0
        })
    }

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
                title: "Bangumi"
                subtitle: "搜索远端条目，或查看账户收藏。"
            }

            Row {
                spacing: 10

                AppButton {
                    text: "搜索"
                    primary: root.currentTab === 0
                    onClicked: root.currentTab = 0
                }

                AppButton {
                    text: "我的收藏"
                    primary: root.currentTab === 1
                    onClicked: root.currentTab = 1
                }

                Rectangle {
                    width: 1
                    height: 28
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.border
                }

                AppText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.currentTab === 1 ? "Fixture 账户：已登录" : "公开搜索"
                    color: Theme.textMuted
                    font.pixelSize: Theme.captionSize
                }
            }

            TextField {
                id: searchField
                width: Math.min(520, parent.width)
                height: 40
                placeholderText: root.currentTab === 0
                                 ? "搜索 Bangumi 动画条目"
                                 : "筛选我的收藏"
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

            SectionHeader {
                width: parent.width
                title: root.currentTab === 0 ? "搜索结果" : "在看与收藏"
                detail: root.visibleSubjects.length + " 个条目"
            }

            Flow {
                width: parent.width
                height: childrenRect.height
                spacing: 16

                Repeater {
                    model: root.visibleSubjects

                    SubjectCard {
                        subject: modelData
                        onActivated: selected => root.openSubject(selected)
                    }
                }
            }

            Item { width: 1; height: 16 }
        }
    }
}
