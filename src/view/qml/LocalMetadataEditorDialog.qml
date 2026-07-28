import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root

    property bool editMode: false
    property bool busy: false
    property var metadata: ({})

    signal saveRequested(string displayTitle, string originalTitle,
                         string summary, string coverUrl,
                         string episodeTitle, string episodeNumber)

    anchors.centerIn: parent
    width: Math.min(580, parent ? parent.width - Theme.pageMargin * 2 : 580)
    height: Math.min(600, parent ? parent.height - Theme.pageMargin * 2 : 600)
    modal: true
    title: editMode ? "编辑数据库元数据" : "创建本地条目"
    padding: 20

    palette.window: Theme.surfaceRaised
    palette.windowText: Theme.text
    palette.button: Theme.surface
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText

    background: Rectangle {
        radius: Theme.radius
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.border
    }

    onOpened: {
        const value = root.metadata || ({})
        displayTitleField.text = value.displayTitle || ""
        originalTitleField.text = value.originalTitle || ""
        summaryField.text = value.summary || ""
        coverField.text = value.coverUrl || ""
        episodeTitleField.text = value.episodeTitle || ""
        episodeNumberField.text = value.episodeNumber || ""
        displayTitleField.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: metadataForm.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: metadataForm
                width: parent.width
                spacing: 9

                AppText {
                    Layout.fillWidth: true
                    text: root.editMode
                          ? "这里修改的是本地数据库副本；若条目仍关联 Bangumi，后续刷新可以按 Bangumi 数据覆盖。清空可选字段会删除本地原值。"
                          : "这些内容只写入本地数据库，不会上传到 Bangumi。字段不会从文件名或文件夹名猜测。"
                    color: Theme.textMuted
                    font.pixelSize: Theme.captionSize
                    wrapMode: Text.Wrap
                }

                AppText {
                    text: "显示标题（必填）"
                    color: Theme.textMuted
                    font.pixelSize: Theme.captionSize
                }

                AppTextField {
                    id: displayTitleField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    placeholderText: "输入条目标题"
                    enabled: !root.busy
                }

                AppText {
                    text: "原文标题（可选）"
                    color: Theme.textMuted
                    font.pixelSize: Theme.captionSize
                }

                AppTextField {
                    id: originalTitleField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    placeholderText: "例如日文或英文标题"
                    enabled: !root.busy
                }

                AppText {
                    text: "简介（可选）"
                    color: Theme.textMuted
                    font.pixelSize: Theme.captionSize
                }

                TextArea {
                    id: summaryField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 92
                    color: Theme.text
                    placeholderText: "输入条目简介"
                    placeholderTextColor: Theme.textFaint
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    enabled: !root.busy
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.surface
                        border.width: 1
                        border.color: summaryField.activeFocus
                                      ? Theme.accent : Theme.border
                    }
                }

                AppText {
                    text: "封面 URL（可选）"
                    color: Theme.textMuted
                    font.pixelSize: Theme.captionSize
                }

                AppTextField {
                    id: coverField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    placeholderText: "https://… 或 file:///…"
                    enabled: !root.busy
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: !root.editMode
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        AppText {
                            text: "章节标题（可选）"
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                        }

                        AppTextField {
                            id: episodeTitleField
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            placeholderText: "不填则显示标题待公布"
                            enabled: !root.busy
                        }
                    }

                    ColumnLayout {
                        Layout.preferredWidth: 150
                        spacing: 6

                        AppText {
                            text: "章节序号（可选）"
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                        }

                        AppTextField {
                            id: episodeNumberField
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            placeholderText: "例如 1"
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            validator: DoubleValidator {
                                bottom: 0
                                top: 100000
                                decimals: 3
                                notation: DoubleValidator.StandardNotation
                            }
                            enabled: !root.busy
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        RowLayout {
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }

            AppButton {
                text: "取消"
                enabled: !root.busy
                onClicked: root.close()
            }

            AppButton {
                text: root.busy
                      ? "正在保存…"
                      : (root.editMode ? "保存修改" : "保存并关联")
                primary: true
                enabled: !root.busy
                         && displayTitleField.text.trim().length > 0
                         && (episodeNumberField.text.trim().length === 0
                             || episodeNumberField.acceptableInput)
                onClicked: {
                    Qt.inputMethod.commit()
                    root.saveRequested(
                        displayTitleField.text,
                        originalTitleField.text,
                        summaryField.text,
                        coverField.text,
                        episodeTitleField.text,
                        episodeNumberField.text)
                }
            }
        }
    }
}
