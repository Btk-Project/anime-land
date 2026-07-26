import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property var subject: FixtureData.subjects[0]
    signal backRequested()

    Rectangle {
        anchors.fill: parent
        color: "#0c0d0e"

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                color: Theme.sidebar

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 20
                    spacing: 14

                    AppButton {
                        text: "返回"
                        quiet: true
                        onClicked: root.backRequested()
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: 3

                        AppText {
                            width: parent.width
                            text: root.subject.title
                            color: Theme.text
                            font.pixelSize: Theme.bodySize
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        AppText {
                            width: parent.width
                            text: root.subject.episode
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            elide: Text.ElideRight
                        }
                    }

                    AppText {
                        text: "Fixture Player"
                        color: Theme.textFaint
                        font.pixelSize: Theme.captionSize
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#090a0b"

                    AppText {
                        anchors.centerIn: parent
                        text: "视频输出区域\nVideoOutputItem 尚未接入"
                        color: Theme.textFaint
                        font.pixelSize: Theme.bodySize
                        horizontalAlignment: Text.AlignHCenter
                        lineHeight: 1.5
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 92
                        color: "#e6151719"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 20
                            anchors.rightMargin: 20
                            anchors.topMargin: 12
                            anchors.bottomMargin: 12
                            spacing: 10

                            Slider {
                                id: progressSlider
                                Layout.fillWidth: true
                                from: 0
                                to: 1
                                value: root.subject.progress

                                background: Rectangle {
                                    x: progressSlider.leftPadding
                                    y: progressSlider.topPadding
                                       + progressSlider.availableHeight / 2 - height / 2
                                    width: progressSlider.availableWidth
                                    height: 4
                                    radius: 2
                                    color: Theme.border

                                    Rectangle {
                                        width: progressSlider.visualPosition * parent.width
                                        height: parent.height
                                        radius: parent.radius
                                        color: Theme.accent
                                    }
                                }

                                handle: Rectangle {
                                    x: progressSlider.leftPadding
                                       + progressSlider.visualPosition
                                       * (progressSlider.availableWidth - width)
                                    y: progressSlider.topPadding
                                       + progressSlider.availableHeight / 2 - height / 2
                                    width: 13
                                    height: 13
                                    radius: 7
                                    color: Theme.text
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                AppButton { text: "播放"; quiet: true }
                                AppButton { text: "后退 10s"; quiet: true }
                                AppButton { text: "前进 10s"; quiet: true }

                                AppText {
                                    text: "15:22 / 24:10"
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.captionSize
                                }

                                Item { Layout.fillWidth: true }

                                AppButton { text: "音量"; quiet: true }
                                AppButton { text: "字幕"; quiet: true }
                                AppButton { text: "全屏"; quiet: true }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 278
                    Layout.fillHeight: true
                    color: Theme.sidebar
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10

                        AppText {
                            text: "章节队列"
                            color: Theme.text
                            font.pixelSize: Theme.headingSize
                            font.weight: Font.DemiBold
                        }

                        AppText {
                            text: "已关联媒体优先"
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 6
                            model: FixtureData.episodes

                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                width: ListView.view.width
                                height: 52
                                radius: Theme.radiusSmall
                                color: index === 3 ? Theme.surfaceRaised : Theme.surface
                                border.width: 1
                                border.color: index === 3 ? Theme.accent : Theme.border

                                Column {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 4

                                    AppText {
                                        width: parent.width
                                        text: "EP" + modelData.number + "  " + modelData.title
                                        color: Theme.text
                                        font.pixelSize: Theme.captionSize
                                        font.weight: index === 3 ? Font.DemiBold : Font.Normal
                                        elide: Text.ElideRight
                                    }

                                    AppText {
                                        width: parent.width
                                        text: modelData.source
                                        color: Theme.textFaint
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Shortcut {
        sequence: StandardKey.Cancel
        onActivated: root.backRequested()
    }
}
