import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "subjectDetailPage"

    property var subject: uiFixtureMode ? FixtureData.subjects[0] : ({})
    property bool summaryExpanded: false
    property string fixtureNotice: ""
    property var displaySubject: uiLongMetadataSmokeTest
            ? ({
                title: "恋爱游戏世界对路人角色很不友好 第二季",
                subtitle: "乙女ゲー世界はモブに厳しい世界です2",
                meta: "2026-07-08 · 2026 / 2026年7月 / Engi / TV",
                summary: "转生至某个剑与魔法『女性向游戏』世界的前社会人·里昂，在这个极度女尊男卑的世界里，他唯一仅存的武器，就是前世被妹妹半强迫玩过的这款游戏的知识。他凭借运用这份知识的行动，试图在这个不讲理的世界中求生。里昂与本应作为主角立于帅哥军团中心的奥莉薇亚，以及本应成为欺凌她的恶役千金的安洁莉卡，培养出深厚的友情。虽然一路遭到不知为何霸占主角位置的玛丽耶妨碍，最终仍一路晋升至子爵之位。然而，由于里昂大幅改变了游戏的走向，这个世界也渐渐开始显露出原作中未曾描述的另一面。接下来，他必须继续面对贵族社会、冒险与选择带来的复杂后果。",
                coverUrl: FixtureData.subjects[0].coverUrl,
                color: FixtureData.subjects[0].color,
                score: "—",
                progress: 0
            })
            : uiFixtureMode
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
        root.summaryExpanded = false
        if (uiFixtureMode || !subjectDetailsViewModel)
            return
        if (root.subject && root.subject.subjectId > 0)
            subjectDetailsViewModel.openSubject(root.subject.subjectId)
        else if (root.subject && root.subject.bangumiId > 0)
            subjectDetailsViewModel.openBangumiSubject(root.subject.bangumiId)
        else
            subjectDetailsViewModel.clear()
    }

    Component {
        id: episodePaginationComponent

        Rectangle {
            id: paginationRoot
            readonly property int displayedPage: uiPaginationSmokeTest
                    ? 17 : (subjectDetailsViewModel
                            ? subjectDetailsViewModel.currentEpisodePage : 0)
            readonly property int displayedPageCount: uiPaginationSmokeTest
                    ? 50 : (subjectDetailsViewModel
                            ? subjectDetailsViewModel.episodePageCount : 0)
            implicitHeight: 58
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                AppButton {
                    text: "正序"
                    primary: uiPaginationSmokeTest
                             || (subjectDetailsViewModel
                                 && !subjectDetailsViewModel
                                        .episodeSortDescending)
                    enabled: uiPaginationSmokeTest
                             || (subjectDetailsViewModel
                                 && !subjectDetailsViewModel.loadingMore)
                    onClicked: {
                        if (subjectDetailsViewModel)
                            subjectDetailsViewModel
                                .setEpisodeSortDescending(false)
                    }
                }

                AppButton {
                    text: "倒序"
                    primary: subjectDetailsViewModel
                             && subjectDetailsViewModel
                                    .episodeSortDescending
                    enabled: uiPaginationSmokeTest
                             || (subjectDetailsViewModel
                                 && !subjectDetailsViewModel.loadingMore)
                    onClicked: {
                        if (subjectDetailsViewModel)
                            subjectDetailsViewModel
                                .setEpisodeSortDescending(true)
                    }
                }

                Item { Layout.fillWidth: true }

                AppText {
                    text: "第 " + paginationRoot.displayedPage + " / "
                          + paginationRoot.displayedPageCount + " 页"
                    color: Theme.textMuted
                    font.pixelSize: Theme.captionSize
                }

                AppButton {
                    text: "上一页"
                    enabled: uiPaginationSmokeTest
                             || (subjectDetailsViewModel
                                 && !subjectDetailsViewModel.loadingMore
                                 && subjectDetailsViewModel
                                        .currentEpisodePage > 1)
                    onClicked: {
                        if (subjectDetailsViewModel)
                            subjectDetailsViewModel.previousEpisodePage()
                    }
                }

                AppButton {
                    text: "下一页"
                    enabled: uiPaginationSmokeTest
                             || (subjectDetailsViewModel
                                 && !subjectDetailsViewModel.loadingMore
                                 && subjectDetailsViewModel.currentEpisodePage
                                    < subjectDetailsViewModel.episodePageCount)
                    onClicked: {
                        if (subjectDetailsViewModel)
                            subjectDetailsViewModel.nextEpisodePage()
                    }
                }

                AppTextField {
                    id: episodePageField
                    Layout.preferredWidth: 74
                    Layout.preferredHeight: 38
                    placeholderText: "页码"
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator {
                        bottom: 1
                        top: uiPaginationSmokeTest ? 50 : subjectDetailsViewModel
                             ? Math.max(1,
                                 subjectDetailsViewModel.episodePageCount)
                             : 1
                    }
                    enabled: uiPaginationSmokeTest
                             || (subjectDetailsViewModel
                                 && !subjectDetailsViewModel.loadingMore)
                    onAccepted: {
                        if (acceptableInput && subjectDetailsViewModel)
                            subjectDetailsViewModel.goToEpisodePage(
                                Number(text))
                    }
                }

                AppButton {
                    text: subjectDetailsViewModel
                          && subjectDetailsViewModel.loadingMore
                          ? "读取中…" : "跳转"
                    enabled: episodePageField.acceptableInput
                             && (uiPaginationSmokeTest
                                 || (subjectDetailsViewModel
                                     && !subjectDetailsViewModel.loadingMore))
                    onClicked: {
                        Qt.inputMethod.commit()
                        if (subjectDetailsViewModel)
                            subjectDetailsViewModel.goToEpisodePage(
                                Number(episodePageField.text))
                    }
                }
            }
        }
    }

    StackView.onActivated: root.loadSubject()

    Timer {
        interval: 180
        running: uiPaginationSmokeTest
        repeat: false
        onTriggered: detailsFlickable.contentY = Math.max(
            0, episodesHeader.y - Theme.pageMargin)
    }

    Connections {
        target: libraryViewModel
        ignoreUnknownSignals: true

        function onLocalMetadataSaved(subjectId) {
            if (!root.displaySubject
                    || subjectId !== root.displaySubject.subjectId)
                return
            metadataEditor.close()
            subjectDetailsViewModel.openSubject(subjectId)
        }

        function onLocalMetadataDeleted(subjectId) {
            if (!root.displaySubject
                    || subjectId !== root.displaySubject.subjectId)
                return
            removeMetadataDialog.close()
            root.backRequested()
        }
    }

    LocalMetadataEditorDialog {
        id: metadataEditor
        editMode: true
        busy: !libraryViewModel || libraryViewModel.associating

        onSaveRequested: function(displayTitle, originalTitle, summary,
                                  coverUrl, episodeTitle, episodeNumber) {
            if (libraryViewModel && root.displaySubject)
                libraryViewModel.updateLocalMetadata(
                    root.displaySubject.subjectId, displayTitle,
                    originalTitle, summary, coverUrl)
        }
    }

    OnlineSourceDialog {
        id: onlineSourceDialog
    }

    Dialog {
        id: removeMetadataDialog
        anchors.centerIn: parent
        width: Math.min(480, root.width - Theme.pageMargin * 2)
        modal: true
        title: "删除数据库元数据条目"
        standardButtons: Dialog.Ok | Dialog.Cancel
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

        onAccepted: {
            if (libraryViewModel && root.displaySubject)
                libraryViewModel.deleteLocalMetadata(
                    root.displaySubject.subjectId)
        }

        Column {
            width: removeMetadataDialog.availableWidth
            spacing: 10

            AppText {
                width: parent.width
                text: root.displaySubject
                      ? "确定删除数据库条目“"
                        + root.displaySubject.title + "”？"
                      : "确定删除这个数据库条目？"
                color: Theme.text
                font.pixelSize: Theme.bodySize
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }

            AppText {
                width: parent.width
                text: "本地章节和媒体关联会一并移除，视频文件不会删除。来自 Bangumi 的条目以后仍可重新获取。"
                color: Theme.textMuted
                font.pixelSize: Theme.captionSize
                wrapMode: Text.Wrap
            }
        }
    }

    Flickable {
        id: detailsFlickable
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
                          ? "正在读取条目…"
                          : !uiFixtureMode && subjectDetailsViewModel
                            && subjectDetailsViewModel.refreshing
                            ? "条目详情 · 正在后台刷新 Bangumi"
                            : "条目详情"
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
                id: subjectOverview
                property int contentPadding: 22
                width: parent.width
                height: Math.max(278,
                                 overviewDetails.implicitHeight
                                 + contentPadding * 2)
                visible: root.contentReady
                radius: Theme.radiusLarge
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: subjectOverview.contentPadding
                    spacing: 24

                    CoverImage {
                        Layout.preferredWidth: 164
                        Layout.preferredHeight: 232
                        Layout.maximumHeight: 232
                        Layout.alignment: Qt.AlignTop
                        radius: Theme.radius
                        source: root.displaySubject.coverUrl || ""
                        title: root.displaySubject.title || ""
                        fallbackColor: root.displaySubject.color
                                       || Theme.surfaceRaised
                    }

                    Column {
                        id: overviewDetails
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: 10

                        SelectableText {
                            width: parent.width
                            text: root.displaySubject.title || ""
                            color: Theme.text
                            font.pixelSize: 28
                            font.weight: Font.DemiBold
                            wrapMode: TextEdit.WordWrap
                            clip: true
                            height: Math.min(implicitHeight, 76)
                        }

                        SelectableText {
                            width: parent.width
                            text: root.displaySubject.subtitle || ""
                            color: Theme.textMuted
                            font.pixelSize: Theme.bodySize
                            wrapMode: TextEdit.WordWrap
                            clip: true
                            height: Math.min(implicitHeight, 48)
                        }

                        SelectableText {
                            width: parent.width
                            text: uiFixtureMode
                                  ? (root.displaySubject.meta + "  ·  Bangumi "
                                     + root.displaySubject.score)
                                  : (root.displaySubject.meta || "本地目录条目")
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            wrapMode: TextEdit.WordWrap
                            clip: true
                            height: Math.min(implicitHeight, 44)
                        }

                        SelectableText {
                            id: summaryText
                            width: parent.width
                            text: root.displaySubject.summary || "暂无简介"
                            color: Theme.text
                            opacity: 0.9
                            font.pixelSize: Theme.bodySize
                            wrapMode: Text.WordWrap
                            height: root.summaryExpanded
                                    ? implicitHeight
                                    : Math.min(implicitHeight, 98)
                            clip: true
                        }

                        AppButton {
                            visible: summaryText.implicitHeight > 98
                            text: root.summaryExpanded
                                  ? "收起简介" : "展开完整简介"
                            quiet: true
                            onClicked: root.summaryExpanded =
                                       !root.summaryExpanded
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
                                visible: uiLongMetadataSmokeTest
                                         || !uiFixtureMode
                                text: "编辑元数据"
                                enabled: libraryViewModel
                                         && subjectDetailsViewModel
                                         && !libraryViewModel.associating
                                         && !subjectDetailsViewModel.loading
                                         && !subjectDetailsViewModel.refreshing
                                onClicked: {
                                    metadataEditor.metadata = ({
                                        subjectId:
                                            root.displaySubject.subjectId,
                                        displayTitle:
                                            root.displaySubject.title || "",
                                        originalTitle:
                                            root.displaySubject.subtitle || "",
                                        summary:
                                            root.displaySubject.summary || "",
                                        coverUrl:
                                            root.displaySubject.coverUrl || ""
                                    })
                                    metadataEditor.open()
                                }
                            }

                            AppButton {
                                visible: uiLongMetadataSmokeTest
                                         || !uiFixtureMode
                                text: "删除条目"
                                enabled: libraryViewModel
                                         && subjectDetailsViewModel
                                         && !libraryViewModel.associating
                                         && !subjectDetailsViewModel.loading
                                         && !subjectDetailsViewModel.refreshing
                                onClicked: removeMetadataDialog.open()
                            }

                            AppButton {
                                visible: uiFixtureMode
                                text: "收藏状态"
                                quiet: true
                                onClicked: root.fixtureNotice =
                                           "Fixture：Bangumi 收藏更新尚未接入"
                            }
                        }

                        SelectableText {
                            width: parent.width
                            text: root.statusMessage
                            color: !uiFixtureMode && subjectDetailsViewModel
                                   && subjectDetailsViewModel
                                          .errorMessage.length > 0
                                   ? Theme.danger : Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            visible: root.statusMessage.length > 0
                            wrapMode: Text.Wrap
                            height: visible ? implicitHeight : 0
                        }
                    }
                }
            }

            SectionHeader {
                id: episodesHeader
                width: parent.width
                visible: root.contentReady
                title: "章节"
                detail: uiPaginationSmokeTest
                        ? "第 17 / 50 页 · 本页 24 / 1191 个数据库章节"
                        : uiFixtureMode
                        ? root.episodeModel.length + " 个 fixture 章节"
                        : "第 "
                          + (subjectDetailsViewModel
                             ? subjectDetailsViewModel.currentEpisodePage : 0)
                          + " / "
                          + (subjectDetailsViewModel
                             ? subjectDetailsViewModel.episodePageCount : 0)
                          + " 页 · 本页 " + root.episodeModel.length + " / "
                          + (subjectDetailsViewModel
                             ? subjectDetailsViewModel.totalEpisodeCount : 0)
                          + " 个数据库章节 · "
                          + (subjectDetailsViewModel
                             ? subjectDetailsViewModel.playableEpisodeCount : 0)
                          + " 个本页有本地媒体"
            }

            Loader {
                width: parent.width
                visible: uiPaginationSmokeTest
                         || (!uiFixtureMode && root.contentReady
                             && subjectDetailsViewModel
                             && subjectDetailsViewModel.episodePageCount > 1)
                sourceComponent: visible ? episodePaginationComponent : null
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
                        onOnlineAction: episode => {
                            if (uiFixtureMode) {
                                root.fixtureNotice =
                                    "Fixture：在线源只在真实详情页按需搜索"
                            }
                            else {
                                onlineSourceDialog.openForEpisode(episode)
                            }
                        }
                    }
                }
            }

            Loader {
                width: parent.width
                visible: uiPaginationSmokeTest
                         || (!uiFixtureMode && subjectDetailsViewModel
                             && subjectDetailsViewModel.episodePageCount > 1)
                sourceComponent: visible ? episodePaginationComponent : null
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
