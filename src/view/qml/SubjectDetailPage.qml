import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property var subject: FixtureData.subjects[0]
    property string notice: ""

    signal backRequested()
    signal playRequested(var subject)

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

            Row {
                spacing: 12

                AppButton {
                    text: "返回"
                    quiet: true
                    onClicked: root.backRequested()
                }

                AppText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "条目详情"
                    color: Theme.textFaint
                    font.pixelSize: Theme.captionSize
                }
            }

            Rectangle {
                width: parent.width
                height: 278
                radius: Theme.radiusLarge
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 24

                    Rectangle {
                        Layout.preferredWidth: 156
                        Layout.fillHeight: true
                        radius: Theme.radius
                        color: root.subject.color

                        AppText {
                            anchors.centerIn: parent
                            text: root.subject.title.slice(0, 1)
                            color: "#e2e5e7"
                            opacity: 0.75
                            font.pixelSize: 64
                            font.weight: Font.Light
                        }
                    }

                    Column {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 10

                        AppText {
                            width: parent.width
                            text: root.subject.title
                            color: Theme.text
                            font.pixelSize: 28
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        AppText {
                            width: parent.width
                            text: root.subject.subtitle
                            color: Theme.textMuted
                            font.pixelSize: Theme.bodySize
                            elide: Text.ElideRight
                        }

                        AppText {
                            width: parent.width
                            text: root.subject.meta + "  ·  Bangumi " + root.subject.score
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            elide: Text.ElideRight
                        }

                        AppText {
                            width: parent.width
                            text: root.subject.summary
                            color: Theme.text
                            opacity: 0.9
                            font.pixelSize: Theme.bodySize
                            lineHeight: 1.35
                            wrapMode: Text.WordWrap
                        }

                        Row {
                            spacing: 10

                            AppButton {
                                text: root.subject.progress > 0 ? "继续播放" : "开始播放"
                                primary: true
                                onClicked: root.playRequested(root.subject)
                            }

                            AppButton {
                                text: "关联媒体"
                                onClicked: root.notice = "Fixture：快速关联文件将在 LibraryStore 接入后启用"
                            }

                            AppButton {
                                text: "收藏状态"
                                quiet: true
                                onClicked: root.notice = "Fixture：Bangumi 收藏更新尚未接入"
                            }
                        }

                        AppText {
                            text: root.notice
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            visible: root.notice.length > 0
                        }
                    }
                }
            }

            SectionHeader {
                width: parent.width
                title: "章节"
                detail: "6 个 fixture 章节"
            }

            Column {
                width: parent.width
                spacing: 8

                Repeater {
                    model: FixtureData.episodes

                    EpisodeRow {
                        width: parent.width
                        episode: modelData
                        onPrimaryAction: episode => {
                            if (episode.linked)
                                root.playRequested(root.subject)
                            else
                                root.notice = "Fixture：该章节尚未关联媒体"
                        }
                    }
                }
            }

            Item { width: 1; height: 16 }
        }
    }
}
