import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    signal openSubject(var subject)
    signal playRequested(var subject)

    readonly property var featured: FixtureData.subjects[0]

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
                subtitle: "继续上次的进度，或从媒体库挑一部动画。"
            }

            Rectangle {
                width: parent.width
                height: 226
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
                            text: "继续观看"
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            font.weight: Font.DemiBold
                        }

                        AppText {
                            width: parent.width
                            text: root.featured.title
                            color: Theme.text
                            font.pixelSize: 28
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        AppText {
                            width: parent.width
                            text: root.featured.episode
                            color: Theme.textMuted
                            font.pixelSize: Theme.bodySize
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            width: Math.min(420, parent.width)
                            height: 4
                            radius: 2
                            color: Theme.border

                            Rectangle {
                                width: parent.width * root.featured.progress
                                height: parent.height
                                radius: parent.radius
                                color: Theme.accent
                            }
                        }

                        Row {
                            spacing: 10

                            AppButton {
                                text: "继续播放"
                                primary: true
                                onClicked: root.playRequested(root.featured)
                            }

                            AppButton {
                                text: "查看详情"
                                onClicked: root.openSubject(root.featured)
                            }
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 250
                        Layout.fillHeight: true
                        radius: Theme.radius
                        color: root.featured.color

                        AppText {
                            anchors.centerIn: parent
                            text: root.featured.title.slice(0, 1)
                            color: "#e2e5e7"
                            opacity: 0.74
                            font.pixelSize: 76
                            font.weight: Font.Light
                        }

                        AppText {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 16
                            text: root.featured.subtitle
                            color: Theme.text
                            font.pixelSize: Theme.captionSize
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            SectionHeader {
                width: parent.width
                title: "最近加入"
                detail: "fixture 数据"
            }

            Flow {
                width: parent.width
                height: childrenRect.height
                spacing: 16

                Repeater {
                    model: FixtureData.subjects.slice(1, 6)

                    SubjectCard {
                        subject: modelData
                        onActivated: selected => root.openSubject(selected)
                    }
                }
            }

            SectionHeader {
                width: parent.width
                title: "最近播放"
                detail: "3 条记录"
            }

            Rectangle {
                width: parent.width
                height: 96
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 14

                    Repeater {
                        model: FixtureData.subjects.slice(0, 3)

                        Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: Theme.radiusSmall
                            color: Theme.surfaceRaised

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 12

                                Rectangle {
                                    Layout.preferredWidth: 42
                                    Layout.fillHeight: true
                                    radius: 4
                                    color: modelData.color
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
                                        text: modelData.episode
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.captionSize
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 8 }
        }
    }
}
