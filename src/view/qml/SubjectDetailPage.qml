import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property var subject: FixtureData.subjects[0]
    property string fixtureNotice: ""
    property var displaySubject: uiFixtureMode
            ? subject
            : (subjectDetailsViewModel
               ? subjectDetailsViewModel.subject : ({}))
    property var episodeModel: uiFixtureMode
            ? FixtureData.episodes
            : (subjectDetailsViewModel
               ? subjectDetailsViewModel.episodes : [])
    property string statusMessage: {
        if (uiFixtureMode)
            return fixtureNotice
        if (!subjectDetailsViewModel)
            return "条目详情服务未初始化"
        if (subjectDetailsViewModel.errorMessage.length > 0)
            return subjectDetailsViewModel.errorMessage
        return subjectDetailsViewModel.noticeMessage
    }
    property bool contentReady: uiFixtureMode
            || (subjectDetailsViewModel
                && subjectDetailsViewModel.hasSubject)
    property bool hasRemoteIdentity: !uiFixtureMode && subject
            && subject.bangumiId > 0

    signal backRequested()
    signal playRequested(var subject)
    signal libraryRequested()

    function loadSubject() {
        if (uiFixtureMode || !subjectDetailsViewModel)
            return
        if (root.subject && root.subject.subjectId > 0)
            subjectDetailsViewModel.openSubject(root.subject.subjectId)
        else if (root.subject && root.subject.bangumiId > 0)
            subjectDetailsViewModel.openBangumiSubject(root.subject.bangumiId)
        else
            subjectDetailsViewModel.clear()
    }

    Component.onCompleted: root.loadSubject()
    StackView.onActivated: root.loadSubject()

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
                    text: !uiFixtureMode && subjectDetailsViewModel
                          && subjectDetailsViewModel.loading
                          ? "正在读取本地条目…" : "条目详情"
                    color: Theme.textFaint
                    font.pixelSize: Theme.captionSize
                }
            }

            Rectangle {
                width: parent.width
                height: 150
                visible: !root.contentReady
                         && (!subjectDetailsViewModel
                             || !subjectDetailsViewModel.loading)
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                Column {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 40, 620)
                    spacing: 8

                    AppText {
                        width: parent.width
                        text: root.statusMessage.length > 0
                              ? root.statusMessage
                              : "本地数据库中没有这个条目"
                        color: Theme.text
                        font.pixelSize: Theme.bodySize
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }

                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 10

                        AppButton {
                            text: root.hasRemoteIdentity
                                  ? "重试读取" : "前往媒体库关联"
                            primary: true
                            onClicked: {
                                if (root.hasRemoteIdentity
                                        && subjectDetailsViewModel) {
                                    subjectDetailsViewModel
                                        .openBangumiSubject(
                                            root.subject.bangumiId)
                                }
                                else {
                                    root.libraryRequested()
                                }
                            }
                        }

                        AppButton {
                            visible: root.hasRemoteIdentity
                            text: "前往媒体库关联"
                            onClicked: root.libraryRequested()
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 278
                visible: root.contentReady
                radius: Theme.radiusLarge
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 24

                    Rectangle {
                        Layout.preferredWidth: 164
                        Layout.preferredHeight: 232
                        Layout.maximumHeight: 232
                        Layout.alignment: Qt.AlignVCenter
                        radius: Theme.radius
                        color: root.displaySubject.color || Theme.surfaceRaised
                        clip: true

                        Image {
                            id: coverImage
                            anchors.fill: parent
                            source: root.displaySubject.coverUrl || ""
                            sourceSize: Qt.size(
                                Math.ceil(width * Screen.devicePixelRatio),
                                Math.ceil(height * Screen.devicePixelRatio))
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: true
                            mipmap: true
                            autoTransform: true
                            visible: status === Image.Ready
                        }

                        AppText {
                            anchors.centerIn: parent
                            text: root.displaySubject.title
                                  ? root.displaySubject.title.slice(0, 1) : "?"
                            visible: !coverImage.visible
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
                            text: root.displaySubject.title || ""
                            color: Theme.text
                            font.pixelSize: 28
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        AppText {
                            width: parent.width
                            text: root.displaySubject.subtitle || ""
                            color: Theme.textMuted
                            font.pixelSize: Theme.bodySize
                            elide: Text.ElideRight
                        }

                        AppText {
                            width: parent.width
                            text: uiFixtureMode
                                  ? (root.displaySubject.meta + "  ·  Bangumi "
                                     + root.displaySubject.score)
                                  : (root.displaySubject.meta || "本地目录条目")
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            elide: Text.ElideRight
                        }

                        AppText {
                            width: parent.width
                            text: root.displaySubject.summary || "暂无简介"
                            color: Theme.text
                            opacity: 0.9
                            font.pixelSize: Theme.bodySize
                            lineHeight: 1.35
                            wrapMode: Text.WordWrap
                            maximumLineCount: 5
                            elide: Text.ElideRight
                        }

                        Row {
                            spacing: 10

                            AppButton {
                                text: !uiFixtureMode && subjectDetailsViewModel
                                      && subjectDetailsViewModel.playing
                                      ? "正在打开…"
                                      : (uiFixtureMode
                                         ? (root.displaySubject.progress > 0
                                            ? "继续播放" : "开始播放")
                                         : (subjectDetailsViewModel
                                            && subjectDetailsViewModel
                                                   .playableEpisodeCount > 0
                                            ? "开始播放"
                                            : "暂无可播放媒体"))
                                primary: true
                                enabled: uiFixtureMode
                                         || (subjectDetailsViewModel
                                             && subjectDetailsViewModel
                                                    .playableEpisodeCount > 0
                                             && !subjectDetailsViewModel.playing)
                                onClicked: {
                                    if (uiFixtureMode)
                                        root.playRequested(root.subject)
                                    else
                                        subjectDetailsViewModel
                                            .playFirstAvailable()
                                }
                            }

                            AppButton {
                                text: "关联媒体"
                                onClicked: {
                                    if (uiFixtureMode)
                                        root.fixtureNotice = "Fixture：关联入口占位"
                                    else
                                        root.libraryRequested()
                                }
                            }

                            AppButton {
                                visible: uiFixtureMode
                                text: "收藏状态"
                                quiet: true
                                onClicked: root.fixtureNotice =
                                           "Fixture：Bangumi 收藏更新尚未接入"
                            }
                        }

                        AppText {
                            width: parent.width
                            text: root.statusMessage
                            color: !uiFixtureMode && subjectDetailsViewModel
                                   && subjectDetailsViewModel
                                          .errorMessage.length > 0
                                   ? Theme.danger : Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            visible: root.statusMessage.length > 0
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            SectionHeader {
                width: parent.width
                visible: root.contentReady
                title: "章节"
                detail: uiFixtureMode
                        ? root.episodeModel.length + " 个 fixture 章节"
                        : root.episodeModel.length + " / "
                          + (subjectDetailsViewModel
                             ? subjectDetailsViewModel.totalEpisodeCount : 0)
                          + " 个数据库章节 · "
                          + (subjectDetailsViewModel
                             ? subjectDetailsViewModel.playableEpisodeCount : 0)
                          + " 个可播放"
            }

            Column {
                width: parent.width
                spacing: 8
                visible: root.contentReady

                Repeater {
                    model: root.episodeModel

                    EpisodeRow {
                        width: parent.width
                        episode: modelData
                        onPrimaryAction: episode => {
                            if (uiFixtureMode) {
                                if (episode.linked)
                                    root.playRequested(root.subject)
                                else
                                    root.fixtureNotice =
                                        "Fixture：该章节尚未关联媒体"
                            }
                            else if (episode.linked) {
                                subjectDetailsViewModel.playEpisode(episode.id)
                            }
                            else {
                                root.libraryRequested()
                            }
                        }
                    }
                }
            }

            AppButton {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !uiFixtureMode && subjectDetailsViewModel
                         && subjectDetailsViewModel.hasMoreEpisodes
                text: subjectDetailsViewModel
                      && subjectDetailsViewModel.loadingMore
                      ? "正在加载…" : "再加载 24 章"
                enabled: subjectDetailsViewModel
                         && !subjectDetailsViewModel.loadingMore
                onClicked: subjectDetailsViewModel.loadMoreEpisodes()
            }

            Rectangle {
                width: parent.width
                height: 110
                visible: root.contentReady && root.episodeModel.length === 0
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                AppText {
                    anchors.centerIn: parent
                    text: "这个本地条目还没有章节数据"
                    color: Theme.textMuted
                    font.pixelSize: Theme.bodySize
                }
            }

            Item { width: 1; height: 16 }
        }
    }
}
