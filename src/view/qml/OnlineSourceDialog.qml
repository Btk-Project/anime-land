import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root

    property var selectedEpisode: ({})

    anchors.centerIn: parent
    width: Math.min(900, parent ? parent.width - Theme.pageMargin * 2 : 900)
    height: Math.min(720, parent ? parent.height - Theme.pageMargin * 2 : 720)
    modal: true
    title: "在线资源"
    padding: 0
    closePolicy: Popup.CloseOnEscape

    palette.window: Theme.surfaceRaised
    palette.windowText: Theme.text
    palette.base: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.surface
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText

    function openForEpisode(episode) {
        root.selectedEpisode = episode || ({})
        if (episodeResourcesViewModel && episode && episode.id > 0)
            episodeResourcesViewModel.openEpisode(episode.id)
        root.open()
    }

    onClosed: {
        if (episodeResourcesViewModel)
            episodeResourcesViewModel.clear()
    }

    Connections {
        target: episodeResourcesViewModel
        enabled: !!episodeResourcesViewModel
        ignoreUnknownSignals: true

        function onPlaybackOpened() {
            root.close()
        }
    }

    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.border
    }

    header: Rectangle {
        implicitHeight: 78
        color: "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 16
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                AppText {
                    Layout.fillWidth: true
                    text: "选择在线资源"
                    color: Theme.text
                    font.pixelSize: Theme.headingSize
                    font.weight: Font.DemiBold
                }

                AppText {
                    Layout.fillWidth: true
                    text: {
                        if (episodeResourcesViewModel
                                && episodeResourcesViewModel.episodeReady) {
                            const value = episodeResourcesViewModel.episode
                            return value.subjectTitle + " · "
                                   + (value.number || "章节") + " · "
                                   + (value.episodeTitle || "标题待公布")
                        }
                        return root.selectedEpisode
                               ? (root.selectedEpisode.number || "章节") + " · "
                                 + (root.selectedEpisode.title || "正在读取目录…")
                               : "正在读取章节目录…"
                    }
                    color: Theme.textMuted
                    font.pixelSize: Theme.captionSize
                    elide: Text.ElideRight
                }
            }

            AppButton {
                text: "关闭"
                quiet: true
                onClicked: root.close()
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.border
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: sourceControls.implicitHeight + 28
            color: Theme.surface

            ColumnLayout {
                id: sourceControls
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    AppText {
                        text: "搜索范围"
                        color: Theme.text
                        font.pixelSize: Theme.bodySize
                        font.weight: Font.DemiBold
                    }

                    AppText {
                        Layout.fillWidth: true
                        text: episodeResourcesViewModel
                              ? episodeResourcesViewModel.selectedProviderCount
                                + " 个在线源（只搜索当前章节）"
                              : "在线源服务未初始化"
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                    }

                    AppButton {
                        text: episodeResourcesViewModel
                              && episodeResourcesViewModel.searching
                              ? "搜索中…" : "搜索所选在线源"
                        primary: true
                        enabled: episodeResourcesViewModel
                                 && episodeResourcesViewModel.episodeReady
                                 && !episodeResourcesViewModel.busy
                                 && episodeResourcesViewModel
                                        .selectedProviderCount > 0
                        onClicked: episodeResourcesViewModel
                                   .searchSelectedProviders()
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 12

                    Repeater {
                        model: episodeResourcesViewModel
                               ? episodeResourcesViewModel.providers : []

                        CheckBox {
                            id: sourceCheck
                            text: modelData.name
                            checked: modelData.selected
                            enabled: episodeResourcesViewModel
                                     && !episodeResourcesViewModel.busy
                            palette.windowText: Theme.text
                            palette.text: Theme.text
                            palette.highlight: Theme.accent
                            onToggled: {
                                if (episodeResourcesViewModel
                                        && checked !== modelData.selected) {
                                    episodeResourcesViewModel
                                        .setProviderSelected(modelData.key,
                                                             checked)
                                }
                            }
                        }
                    }
                }

                AppText {
                    Layout.fillWidth: true
                    visible: !episodeResourcesViewModel
                             || !episodeResourcesViewModel.hasProviders
                    text: "当前没有已启用的在线源。可在插件目录安装或启用 Episode Provider。"
                    color: Theme.warning
                    font.pixelSize: Theme.captionSize
                    wrapMode: Text.Wrap
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        ScrollView {
            id: resultScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            Column {
                width: resultScroll.availableWidth
                padding: 20
                spacing: 14

                Rectangle {
                    width: parent.width - parent.padding * 2
                    height: 112
                    visible: episodeResourcesViewModel
                             && !episodeResourcesViewModel.busy
                             && episodeResourcesViewModel.episodeReady
                             && episodeResourcesViewModel.providers.every(
                                 function(provider) {
                                     return provider.status === "idle"
                                 })
                    radius: Theme.radius
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border

                    Column {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 32, 620)
                        spacing: 6

                        AppText {
                            width: parent.width
                            text: "在线搜索不会自动开始"
                            color: Theme.text
                            font.pixelSize: Theme.bodySize
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                        }

                        AppText {
                            width: parent.width
                            text: "勾选 1–N 个来源并搜索。结果仅为临时建议，不会写入媒体库或参与找番。"
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }

                Repeater {
                    model: episodeResourcesViewModel
                           ? episodeResourcesViewModel.providers : []

                    Rectangle {
                        id: providerCard
                        property var providerData: modelData

                        width: parent.width - parent.padding * 2
                        implicitHeight: providerContent.implicitHeight + 28
                        visible: providerData.selected
                        radius: Theme.radius
                        color: Theme.surface
                        border.width: 1
                        border.color: providerData.status === "error"
                                      ? Theme.danger : Theme.border

                        Column {
                            id: providerContent
                            x: 14
                            y: 14
                            width: parent.width - 28
                            spacing: 10

                            RowLayout {
                                width: parent.width
                                spacing: 10

                                Rectangle {
                                    Layout.preferredWidth: 28
                                    Layout.preferredHeight: 28
                                    radius: 14
                                    color: Theme.surfaceRaised

                                    AppText {
                                        anchors.centerIn: parent
                                        text: providerCard.providerData.name
                                              .slice(0, 1)
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.captionSize
                                        font.weight: Font.DemiBold
                                    }
                                }

                                AppText {
                                    Layout.fillWidth: true
                                    text: providerCard.providerData.name
                                    color: Theme.text
                                    font.pixelSize: Theme.bodySize
                                    font.weight: Font.DemiBold
                                }

                                BusyIndicator {
                                    Layout.preferredWidth: 24
                                    Layout.preferredHeight: 24
                                    running: providerCard.providerData.status
                                             === "loading"
                                    visible: running
                                }

                                AppText {
                                    text: providerCard.providerData.status
                                          === "loading" ? "正在搜索"
                                          : providerCard.providerData.status
                                            === "ready"
                                            ? providerCard.providerData
                                                .resultCount + " 个建议"
                                          : providerCard.providerData.status
                                            === "empty" ? "无结果"
                                          : providerCard.providerData.status
                                            === "error" ? "搜索失败"
                                          : "等待搜索"
                                    color: providerCard.providerData.status
                                           === "error" ? Theme.danger
                                           : providerCard.providerData.status
                                             === "empty" ? Theme.warning
                                           : Theme.textMuted
                                    font.pixelSize: Theme.captionSize
                                }
                            }

                            AppText {
                                width: parent.width
                                visible: providerCard.providerData.message
                                         .length > 0
                                text: providerCard.providerData.message
                                color: providerCard.providerData.status
                                       === "error" ? Theme.danger
                                       : Theme.textMuted
                                font.pixelSize: Theme.captionSize
                                wrapMode: Text.Wrap
                            }

                            Repeater {
                                model: providerCard.providerData.results

                                Rectangle {
                                    id: resultCard
                                    property var resultData: modelData

                                    width: providerContent.width
                                    implicitHeight: resultContent
                                                    .implicitHeight + 24
                                    radius: Theme.radiusSmall
                                    color: Theme.surfaceRaised
                                    border.width: 1
                                    border.color: Theme.border

                                    RowLayout {
                                        id: resultContent
                                        x: 12
                                        y: 12
                                        width: parent.width - 24
                                        spacing: 12

                                        CoverImage {
                                            Layout.preferredWidth: 58
                                            Layout.preferredHeight: 78
                                            source: resultCard.resultData
                                                    .coverUrl
                                            title: resultCard.resultData
                                                   .subjectTitle
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 4

                                            RowLayout {
                                                Layout.fillWidth: true

                                                AppText {
                                                    Layout.fillWidth: true
                                                    text: resultCard.resultData
                                                          .subjectTitle
                                                          || resultCard
                                                             .resultData
                                                             .displayName
                                                    color: Theme.text
                                                    font.pixelSize:
                                                        Theme.bodySize
                                                    font.weight:
                                                        Font.DemiBold
                                                    elide: Text.ElideRight
                                                }

                                                Rectangle {
                                                    Layout.preferredWidth:
                                                        matchText
                                                        .implicitWidth + 14
                                                    Layout.preferredHeight: 24
                                                    radius: 12
                                                    color: resultCard
                                                           .resultData
                                                           .confidence >= 0.9
                                                           ? Theme.success
                                                           : resultCard
                                                             .resultData
                                                             .confidence >= 0.65
                                                             ? Theme.warning
                                                             : Theme.danger
                                                    opacity: 0.85

                                                    AppText {
                                                        id: matchText
                                                        anchors.centerIn:
                                                            parent
                                                        text: resultCard
                                                              .resultData
                                                              .matchLabel
                                                              + " "
                                                              + Math.round(
                                                                  resultCard
                                                                  .resultData
                                                                  .confidence
                                                                  * 100)
                                                              + "%"
                                                        color: Theme.accentText
                                                        font.pixelSize: 10
                                                        font.weight:
                                                            Font.DemiBold
                                                    }
                                                }
                                            }

                                            AppText {
                                                Layout.fillWidth: true
                                                text: "匹配分集："
                                                      + (resultCard.resultData
                                                         .episodeTitle
                                                         || "未标注")
                                                      + " · 线路："
                                                      + (resultCard.resultData
                                                         .sourceLine
                                                         || "默认")
                                                color: Theme.textMuted
                                                font.pixelSize:
                                                    Theme.captionSize
                                                elide: Text.ElideRight
                                            }

                                            AppText {
                                                Layout.fillWidth: true
                                                visible: resultCard.resultData
                                                         .detail.length > 0
                                                text: resultCard.resultData
                                                      .detail
                                                color: Theme.textFaint
                                                font.pixelSize:
                                                    Theme.captionSize
                                                elide: Text.ElideRight
                                            }

                                            AppText {
                                                Layout.fillWidth: true
                                                text: resultCard.resultData
                                                      .assets.map(
                                                          function(asset) {
                                                              return asset.name
                                                                     + " ["
                                                                     + asset.kind
                                                                     + "/"
                                                                     + asset.streamType
                                                                     + "]"
                                                          }).join(" · ")
                                                color: Theme.textFaint
                                                font.pixelSize: 10
                                                elide: Text.ElideRight
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.alignment:
                                                Qt.AlignVCenter
                                            spacing: 5

                                            AppText {
                                                Layout.alignment:
                                                    Qt.AlignHCenter
                                                text: "临时结果"
                                                color: Theme.warning
                                                font.pixelSize: 10
                                            }

                                            AppButton {
                                                text: episodeResourcesViewModel
                                                      && episodeResourcesViewModel
                                                         .playingHandle
                                                         === resultCard
                                                            .resultData.handle
                                                      ? "解析中…" : "选择并播放"
                                                primary: true
                                                enabled: episodeResourcesViewModel
                                                         && !episodeResourcesViewModel
                                                                .busy
                                                onClicked: episodeResourcesViewModel
                                                           .playOnline(
                                                               resultCard
                                                               .resultData
                                                               .handle)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                AppText {
                    width: parent.width - parent.padding * 2
                    visible: episodeResourcesViewModel
                             && (episodeResourcesViewModel.errorMessage
                                 .length > 0
                                 || episodeResourcesViewModel.noticeMessage
                                    .length > 0)
                    text: !episodeResourcesViewModel ? ""
                          : episodeResourcesViewModel.errorMessage.length > 0
                            ? episodeResourcesViewModel.errorMessage
                            : episodeResourcesViewModel.noticeMessage
                    color: episodeResourcesViewModel
                           && episodeResourcesViewModel.errorMessage.length > 0
                           ? Theme.danger : Theme.textMuted
                    font.pixelSize: Theme.captionSize
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
