import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    signal openSubject(var subject)
    signal backRequested()

    property string activeFilter: "全部"
    property bool showBackNavigation: false
    property string fixtureNotice: ""
    property var pendingRemoval: null
    property var pendingAssociation: null
    property var pendingLocalMetadataDeletion: null
    property bool associationSubjectSelected: false
    property double selectedAssociationSubjectId: 0
    property var associationSmokeSubjects: [{
        bangumiId: 9784,
        title: "名侦探柯南",
        meta: "Bangumi 9784 · 3200 话"
    }]
    property var associationSmokeEpisodes: {
        const episodes = []
        for (let number = 481; number <= 504; ++number) {
            episodes.push({
                id: number,
                number: "EP" + number,
                title: "用于检查大量章节分页与定位的章节标题 " + number,
                episodeNumber: number,
                type: 0
            })
        }
        return episodes
    }
    property var sourceItems: uiFixtureMode
            ? FixtureData.subjects
            : (libraryViewModel ? libraryViewModel.mediaItems : [])
    property var sourceSubjectGroups: !uiFixtureMode && libraryViewModel
            ? libraryViewModel.subjectGroups : []
    property var sourceUnassociatedGroups: !uiFixtureMode && libraryViewModel
            ? libraryViewModel.unassociatedGroups : []
    property var visibleItems: sourceItems.filter(function(item) {
        return root.matchesItem(item)
    })
    property var visibleSubjectGroups: {
        if (uiFixtureMode)
            return []
        const groups = []
        sourceSubjectGroups.forEach(function(subject) {
            const episodes = []
            const mediaIds = ({})
            let mediaCount = 0
            subject.episodes.forEach(function(episode) {
                const items = episode.items.filter(function(item) {
                    return root.matchesItem(item)
                })
                if (items.length > 0) {
                    items.forEach(function(item) {
                        if (!mediaIds[item.id]) {
                            mediaIds[item.id] = true
                            mediaCount += 1
                        }
                    })
                    episodes.push({
                        episodeId: episode.episodeId,
                        title: episode.title,
                        number: episode.number,
                        label: episode.label,
                        totalItemCount: episode.itemCount,
                        items: items
                    })
                }
            })
            if (episodes.length > 0) {
                groups.push({
                    subjectId: subject.subjectId,
                    title: subject.title,
                    totalEpisodeCount: subject.episodeCount,
                    episodeCount: episodes.length,
                    mediaCount: mediaCount,
                    episodes: episodes
                })
            }
        })
        return groups
    }
    property var visibleUnassociatedGroups: {
        if (uiFixtureMode)
            return []
        const groups = []
        sourceUnassociatedGroups.forEach(function(group) {
            const items = group.items.filter(function(item) {
                return root.matchesItem(item)
            })
            if (items.length > 0) {
                groups.push({
                    resourceId: group.resourceId,
                    title: group.title,
                    totalItemCount: group.itemCount,
                    items: items
                })
            }
        })
        return groups
    }

    function matchesItem(item) {
        const query = searchField.text.trim().toLowerCase()
        const searchText = item.searchText
                ? item.searchText : (item.title + " " + item.subtitle)
        const matchesText = query.length === 0
                || searchText.toLowerCase().indexOf(query) >= 0
        if (!matchesText)
            return false
        if (activeFilter === "全部")
            return true
        if (!uiFixtureMode)
            return (activeFilter === "未关联" && item.associationCount === 0)
                    || (activeFilter === "已关联"
                        && item.associationCount > 0)
        return (activeFilter === "在看" && item.progress > 0 && item.progress < 1)
                || (activeFilter === "未观看" && item.progress === 0)
    }
    property string statusMessage: {
        if (uiFixtureMode)
            return fixtureNotice
        if (!libraryViewModel)
            return "本地媒体库未初始化"
        if (libraryViewModel.errorMessage.length > 0)
            return libraryViewModel.errorMessage
        return libraryViewModel.noticeMessage
    }

    function confirmRemoval(media) {
        if (uiFixtureMode || !libraryViewModel || !media
                || libraryViewModel.loading || libraryViewModel.importing
                || libraryViewModel.removing)
            return
        pendingRemoval = media
        removeDialog.open()
    }

    function actionsBusy() {
        return !libraryViewModel || libraryViewModel.loading
                || libraryViewModel.importing || libraryViewModel.removing
                || libraryViewModel.associating || libraryViewModel.playing
    }

    function openAssociation(media) {
        if (uiFixtureMode || !media || actionsBusy())
            return
        pendingAssociation = media
        associationSearch.text = ""
        associationEpisodeLocator.text = ""
        associationEpisodePageField.text = ""
        associationSubjectSelected = false
        selectedAssociationSubjectId = 0
        libraryViewModel.clearAssociationPicker()
        associationDialog.open()
        associationSearch.forceActiveFocus()
    }

    function submitAssociationSearch() {
        if (root.actionsBusy())
            return
        associationSubjectSelected = false
        selectedAssociationSubjectId = 0
        associationEpisodeLocator.text = ""
        associationEpisodePageField.text = ""
        Qt.inputMethod.commit()
        Qt.callLater(function() {
            libraryViewModel.searchAssociationSubjects(
                associationSearch.text)
        })
    }

    Timer {
        interval: 80
        running: uiAssociationSmokeTest || uiCustomMetadataSmokeTest
        repeat: false
        onTriggered: {
            root.pendingAssociation = {
                id: 1,
                title: "oceans.mp4",
                associationCount: 0,
                associations: []
            }
            root.associationSubjectSelected = uiAssociationSmokeTest
            root.selectedAssociationSubjectId = uiAssociationSmokeTest ? 9784 : 0
            associationDialog.open()
            if (uiAssociationSmokeTest)
                Qt.callLater(function() {
                    episodeResults.positionViewAtIndex(19, ListView.Center)
                })
            if (uiCustomMetadataSmokeTest)
                Qt.callLater(function() { customMetadataDialog.open() })
        }
    }

    Connections {
        target: libraryViewModel
        ignoreUnknownSignals: true

        function onAssociationChanged() {
            if (!libraryViewModel)
                return
            if (libraryViewModel.associationEpisodePage > 0)
                associationEpisodePageField.text =
                    String(libraryViewModel.associationEpisodePage)
            const focusIndex =
                libraryViewModel.associationEpisodeFocusIndex
            if (focusIndex >= 0)
                Qt.callLater(function() {
                    episodeResults.positionViewAtIndex(
                        focusIndex, ListView.Center)
                })
        }

        function onLocalMetadataEditorChanged() {
            if (!libraryViewModel
                    || libraryViewModel.localMetadataEditor.subjectId === undefined)
                return
            customMetadataDialog.editMode = true
            customMetadataDialog.metadata =
                libraryViewModel.localMetadataEditor
            customMetadataDialog.open()
        }

        function onLocalMetadataSaved(subjectId) {
            if (customMetadataDialog.visible)
                customMetadataDialog.close()
            if (associationDialog.visible)
                associationDialog.close()
        }

        function onLocalMetadataDeleted(subjectId) {
            root.pendingLocalMetadataDeletion = null
        }
    }

    FileDialog {
        id: importDialog
        title: "选择要导入的媒体文件"
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            "视频文件 (*.mkv *.mp4 *.avi *.mov *.webm *.m4v *.ts *.flv)",
            "所有文件 (*)"
        ]
        onAccepted: {
            if (libraryViewModel)
                libraryViewModel.importFiles(selectedFiles)
        }
    }

    Dialog {
        id: removeDialog
        anchors.centerIn: parent
        width: Math.min(440, root.width - Theme.pageMargin * 2)
        modal: true
        title: "从媒体库移除"
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
            if (root.pendingRemoval && libraryViewModel)
                libraryViewModel.removeMedia(root.pendingRemoval.id)
            root.pendingRemoval = null
        }
        onRejected: root.pendingRemoval = null

        Column {
            width: removeDialog.availableWidth
            spacing: 10

            AppText {
                width: parent.width
                text: root.pendingRemoval
                      ? "确定移除“" + root.pendingRemoval.title + "”？"
                      : "确定移除这个媒体文件？"
                color: Theme.text
                font.pixelSize: Theme.bodySize
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }

            AppText {
                width: parent.width
                text: "只会删除媒体库记录，不会删除磁盘上的原视频；以后仍可重新导入。"
                color: Theme.textMuted
                font.pixelSize: Theme.captionSize
                wrapMode: Text.Wrap
            }
        }
    }

    Dialog {
        id: removeLocalMetadataDialog
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
            if (root.pendingLocalMetadataDeletion && libraryViewModel)
                libraryViewModel.deleteLocalMetadata(
                    root.pendingLocalMetadataDeletion.subjectId)
        }
        onRejected: root.pendingLocalMetadataDeletion = null

        Column {
            width: removeLocalMetadataDialog.availableWidth
            spacing: 10

            AppText {
                width: parent.width
                text: root.pendingLocalMetadataDeletion
                      ? "确定删除数据库条目“"
                        + root.pendingLocalMetadataDeletion.title + "”？"
                      : "确定删除这个本地元数据条目？"
                color: Theme.text
                font.pixelSize: Theme.bodySize
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }

            AppText {
                width: parent.width
                text: "本地章节和媒体关联会一并移除，导入记录和视频文件都会保留。来自 Bangumi 的条目以后仍可重新获取。"
                color: Theme.textMuted
                font.pixelSize: Theme.captionSize
                wrapMode: Text.Wrap
            }
        }
    }

    Dialog {
        id: associationDialog
        anchors.centerIn: parent
        width: Math.min(900, root.width - Theme.pageMargin * 2)
        height: Math.min(590, root.height - Theme.pageMargin * 2)
        modal: true
        title: "关联 Bangumi 章节"
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

        onClosed: {
            pendingAssociation = null
            associationSubjectSelected = false
            selectedAssociationSubjectId = 0
            if (libraryViewModel)
                libraryViewModel.clearAssociationPicker()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            AppText {
                Layout.fillWidth: true
                text: root.pendingAssociation
                      ? "媒体文件：" + root.pendingAssociation.title : ""
                color: Theme.textMuted
                font.pixelSize: Theme.captionSize
                elide: Text.ElideMiddle
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                AppTextField {
                    id: associationSearch
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    placeholderText: "搜索动画名称"
                    onAccepted: {
                        if (!inputMethodComposing)
                            root.submitAssociationSearch()
                    }
                }

                AppButton {
                    text: libraryViewModel && libraryViewModel.associating
                          ? "正在读取…" : "搜索"
                    primary: true
                    enabled: !root.actionsBusy()
                    onPressed: Qt.inputMethod.commit()
                    onClicked: root.submitAssociationSearch()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: currentLinks.implicitHeight + 20
                visible: root.pendingAssociation
                         && root.pendingAssociation.associationCount > 0
                radius: Theme.radiusSmall
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                Column {
                    id: currentLinks
                    x: 10
                    y: 10
                    width: parent.width - 20
                    spacing: 7

                    AppText {
                        text: "当前关联"
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                        font.weight: Font.DemiBold
                    }

                    Repeater {
                        model: root.pendingAssociation
                               ? root.pendingAssociation.associations : []

                        RowLayout {
                            required property var modelData
                            width: currentLinks.width

                            AppText {
                                Layout.fillWidth: true
                                text: modelData.label
                                color: Theme.text
                                font.pixelSize: Theme.captionSize
                                elide: Text.ElideRight
                            }

                            AppButton {
                                text: "解除"
                                quiet: true
                                enabled: !root.actionsBusy()
                                onClicked: {
                                    libraryViewModel.unlinkMedia(
                                                root.pendingAssociation.id,
                                                modelData.episodeId)
                                    associationDialog.close()
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusSmall
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        AppText {
                            text: "Bangumi 条目"
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            font.weight: Font.DemiBold
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ListView {
                                id: subjectResults
                                anchors.fill: parent
                                clip: true
                                spacing: 6
                                model: uiAssociationSmokeTest
                                       ? root.associationSmokeSubjects
                                       : (libraryViewModel
                                          ? libraryViewModel
                                            .associationSubjects : [])

                                delegate: Rectangle {
                                    required property var modelData
                                    width: ListView.view.width
                                    height: 66
                                    radius: Theme.radiusSmall
                                    color: root.selectedAssociationSubjectId
                                           === modelData.bangumiId
                                           ? Theme.surfaceHover
                                           : (subjectMouse.containsMouse
                                              ? Theme.surfaceHover
                                              : Theme.surfaceRaised)
                                    border.width: 1
                                    border.color:
                                        root.selectedAssociationSubjectId
                                        === modelData.bangumiId
                                        ? Theme.accent : Theme.border

                                    Column {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.margins: 10
                                        spacing: 4
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
                                            color: Theme.textFaint
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                    }

                                    MouseArea {
                                        id: subjectMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        enabled: !root.actionsBusy()
                                        onClicked: {
                                            root.associationSubjectSelected = true
                                            root.selectedAssociationSubjectId =
                                                modelData.bangumiId
                                            associationEpisodeLocator.text = ""
                                            associationEpisodePageField.text = ""
                                            libraryViewModel
                                                .selectAssociationSubject(
                                                    modelData.bangumiId)
                                        }
                                    }
                                }
                            }

                            AppText {
                                anchors.centerIn: parent
                                width: parent.width - 24
                                visible: subjectResults.count === 0
                                text: libraryViewModel
                                      && libraryViewModel.associating
                                      ? "正在搜索 Bangumi 条目……"
                                      : libraryViewModel
                                        && libraryViewModel.errorMessage.length > 0
                                        ? libraryViewModel.errorMessage
                                        : associationSearch.text.trim().length === 0
                                          ? "输入动画名称后搜索"
                                          : libraryViewModel
                                            && libraryViewModel.noticeMessage.length > 0
                                            ? libraryViewModel.noticeMessage
                                            : "没有匹配的 Bangumi 条目"
                                color: libraryViewModel
                                       && libraryViewModel.errorMessage.length > 0
                                       ? Theme.danger : Theme.textMuted
                                font.pixelSize: Theme.captionSize
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusSmall
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 7

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            AppText {
                                Layout.fillWidth: true
                                text: {
                                    const total = uiAssociationSmokeTest
                                        ? 3200
                                        : (libraryViewModel
                                           ? libraryViewModel
                                             .associationEpisodeTotal : 0)
                                    return total > 0
                                        ? "章节 · 共 " + total + " 条"
                                        : "章节"
                                }
                                color: Theme.textMuted
                                font.pixelSize: Theme.captionSize
                                font.weight: Font.DemiBold
                            }

                            AppButton {
                                Layout.preferredWidth: 58
                                Layout.preferredHeight: 32
                                text: "正序"
                                primary: !libraryViewModel
                                         || !libraryViewModel
                                             .associationEpisodeDescending
                                enabled: root.associationSubjectSelected
                                         && (uiAssociationSmokeTest
                                             || !root.actionsBusy())
                                onClicked: {
                                    if (libraryViewModel)
                                        libraryViewModel
                                            .setAssociationEpisodeDescending(
                                                false)
                                }
                            }

                            AppButton {
                                Layout.preferredWidth: 58
                                Layout.preferredHeight: 32
                                text: "倒序"
                                primary: libraryViewModel
                                         && libraryViewModel
                                             .associationEpisodeDescending
                                enabled: root.associationSubjectSelected
                                         && (uiAssociationSmokeTest
                                             || !root.actionsBusy())
                                onClicked: {
                                    if (libraryViewModel)
                                        libraryViewModel
                                            .setAssociationEpisodeDescending(
                                                true)
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            AppTextField {
                                id: associationEpisodeLocator
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                placeholderText: "定位章节号，如 500"
                                text: uiAssociationSmokeTest ? "500" : ""
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                validator: DoubleValidator {
                                    bottom: 0.001
                                    top: libraryViewModel
                                         && libraryViewModel
                                             .associationEpisodeTotal > 0
                                         ? libraryViewModel
                                             .associationEpisodeTotal
                                         : 10000000
                                    decimals: 3
                                    notation: DoubleValidator.StandardNotation
                                }
                                enabled: root.associationSubjectSelected
                                         && (uiAssociationSmokeTest
                                             || !root.actionsBusy())
                                onAccepted: {
                                    if (!inputMethodComposing
                                            && libraryViewModel)
                                        libraryViewModel.locateAssociationEpisode(
                                            text)
                                }
                            }

                            AppButton {
                                Layout.preferredWidth: 58
                                Layout.preferredHeight: 34
                                text: "定位"
                                primary: true
                                enabled: associationEpisodeLocator.enabled
                                         && associationEpisodeLocator
                                             .acceptableInput
                                onPressed: Qt.inputMethod.commit()
                                onClicked: {
                                    if (libraryViewModel)
                                        libraryViewModel.locateAssociationEpisode(
                                            associationEpisodeLocator.text)
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ListView {
                                id: episodeResults
                                anchors.fill: parent
                                clip: true
                                spacing: 6
                                model: uiAssociationSmokeTest
                                       ? root.associationSmokeEpisodes
                                       : (libraryViewModel
                                          ? libraryViewModel
                                            .associationEpisodes : [])

                                delegate: Rectangle {
                                    required property var modelData
                                    width: ListView.view.width
                                    height: 52
                                    radius: Theme.radiusSmall
                                    color: Theme.surfaceRaised
                                    border.width: 1
                                    border.color: Theme.border

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 8
                                        spacing: 8
                                        AppText {
                                            Layout.preferredWidth: 48
                                            text: modelData.number
                                            color: Theme.textFaint
                                            font.pixelSize: 11
                                        }
                                        AppText {
                                            Layout.fillWidth: true
                                            text: modelData.title
                                            color: Theme.text
                                            font.pixelSize: Theme.captionSize
                                            elide: Text.ElideRight
                                        }
                                        AppButton {
                                            Layout.preferredWidth: 72
                                            Layout.preferredHeight: 34
                                            text: "关联"
                                            primary: true
                                            enabled: !root.actionsBusy()
                                            onClicked: {
                                                libraryViewModel.linkMedia(
                                                    root.pendingAssociation.id,
                                                    modelData.id)
                                                associationDialog.close()
                                            }
                                        }
                                    }
                                }
                            }

                            AppText {
                                anchors.centerIn: parent
                                width: parent.width - 24
                                visible: episodeResults.count === 0
                                text: !root.associationSubjectSelected
                                      ? "先从左侧选择一个 Bangumi 条目"
                                      : libraryViewModel
                                        && libraryViewModel.associating
                                        ? "正在读取章节……"
                                        : libraryViewModel
                                          && libraryViewModel.errorMessage.length > 0
                                          ? libraryViewModel.errorMessage
                                          : libraryViewModel
                                            && libraryViewModel.noticeMessage.length > 0
                                            ? libraryViewModel.noticeMessage
                                            : "该条目没有可关联章节"
                                color: libraryViewModel
                                       && libraryViewModel.errorMessage.length > 0
                                       ? Theme.danger : Theme.textMuted
                                font.pixelSize: Theme.captionSize
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            visible: root.associationSubjectSelected

                            AppButton {
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 32
                                text: "‹"
                                ToolTip.visible: hovered
                                ToolTip.text: "上一页"
                                enabled: !uiAssociationSmokeTest
                                         && libraryViewModel
                                         && libraryViewModel
                                             .associationEpisodePage > 1
                                         && !root.actionsBusy()
                                onClicked: libraryViewModel
                                    .previousAssociationEpisodePage()
                            }

                            AppText {
                                Layout.preferredWidth: 54
                                text: uiAssociationSmokeTest
                                      ? "21/134"
                                      : (libraryViewModel
                                         ? libraryViewModel
                                           .associationEpisodePage + "/"
                                           + libraryViewModel
                                             .associationEpisodePageCount
                                         : "0/0")
                                color: Theme.textMuted
                                font.pixelSize: Theme.captionSize
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Item { Layout.fillWidth: true }

                            AppTextField {
                                id: associationEpisodePageField
                                Layout.preferredWidth: 48
                                Layout.preferredHeight: 32
                                horizontalAlignment: TextInput.AlignHCenter
                                text: uiAssociationSmokeTest ? "21" : ""
                                placeholderText: "页"
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: IntValidator {
                                    bottom: 1
                                    top: libraryViewModel
                                         ? Math.max(1, libraryViewModel
                                             .associationEpisodePageCount) : 1
                                }
                                enabled: uiAssociationSmokeTest
                                         || (libraryViewModel
                                             && libraryViewModel
                                                 .associationEpisodePageCount
                                                > 1
                                             && !root.actionsBusy())
                                onAccepted: {
                                    if (!inputMethodComposing
                                            && libraryViewModel)
                                        libraryViewModel
                                            .goToAssociationEpisodePage(
                                                parseInt(text))
                                }
                            }

                            AppButton {
                                Layout.preferredWidth: 58
                                Layout.preferredHeight: 32
                                text: "跳转"
                                enabled: associationEpisodePageField.enabled
                                         && associationEpisodePageField
                                             .acceptableInput
                                onPressed: Qt.inputMethod.commit()
                                onClicked: {
                                    if (libraryViewModel)
                                        libraryViewModel
                                            .goToAssociationEpisodePage(
                                                parseInt(
                                                    associationEpisodePageField
                                                        .text))
                                }
                            }

                            AppButton {
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 32
                                text: "›"
                                ToolTip.visible: hovered
                                ToolTip.text: "下一页"
                                enabled: !uiAssociationSmokeTest
                                         && libraryViewModel
                                         && libraryViewModel
                                             .associationEpisodePage
                                            < libraryViewModel
                                              .associationEpisodePageCount
                                         && !root.actionsBusy()
                                onClicked: libraryViewModel
                                    .nextAssociationEpisodePage()
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true

                AppButton {
                    text: "创建本地条目"
                    enabled: root.pendingAssociation !== null
                             && !root.actionsBusy()
                    onClicked: {
                        customMetadataDialog.editMode = false
                        customMetadataDialog.metadata = ({})
                        customMetadataDialog.open()
                    }
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: "取消"
                    onClicked: associationDialog.close()
                }
            }
        }
    }

    LocalMetadataEditorDialog {
        id: customMetadataDialog
        busy: root.actionsBusy()

        onClosed: {
            if (libraryViewModel)
                libraryViewModel.clearLocalMetadataEditor()
        }

        onSaveRequested: function(displayTitle, originalTitle, summary,
                                  coverUrl, episodeTitle, episodeNumber) {
            if (!libraryViewModel)
                return
            if (editMode) {
                libraryViewModel.updateLocalMetadata(
                    metadata.subjectId, displayTitle, originalTitle,
                    summary, coverUrl)
            }
            else if (root.pendingAssociation) {
                libraryViewModel.createCustomMetadata(
                    root.pendingAssociation.id, displayTitle, originalTitle,
                    summary, coverUrl, episodeTitle, episodeNumber)
            }
        }
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
                visible: !root.showBackNavigation
                title: "媒体库"
                subtitle: uiFixtureMode
                          ? "浏览本地条目、观看进度和媒体关联状态。"
                          : "已关联媒体按条目与章节整理，未关联文件保留在待整理区。"
            }

            Row {
                visible: root.showBackNavigation
                height: visible ? 48 : 0
                spacing: 12

                AppButton {
                    text: "返回"
                    quiet: true
                    onClicked: root.backRequested()
                }

                AppText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "选择媒体并关联章节"
                    color: Theme.textMuted
                    font.pixelSize: Theme.bodySize
                }
            }

            Row {
                width: parent.width
                spacing: 10

                AppTextField {
                    id: searchField
                    width: Math.min(300, parent.width - 390)
                    height: 40
                    placeholderText: "搜索本地媒体库"
                }

                Repeater {
                    model: uiFixtureMode
                           ? ["全部", "在看", "未观看"]
                           : ["全部", "未关联", "已关联"]

                    AppButton {
                        text: modelData
                        primary: root.activeFilter === modelData
                        onClicked: root.activeFilter = modelData
                    }
                }

                Item { width: 8; height: 1 }

                AppButton {
                    text: !uiFixtureMode && libraryViewModel
                          && libraryViewModel.importing
                          ? "正在导入…" : "导入媒体"
                    primary: true
                    enabled: uiFixtureMode || (libraryViewModel
                             && !libraryViewModel.loading
                             && !libraryViewModel.importing
                             && !libraryViewModel.removing
                             && !libraryViewModel.associating
                             && !libraryViewModel.playing)
                    onClicked: {
                        if (uiFixtureMode)
                            root.fixtureNotice = "Fixture：导入入口保持为界面调试占位"
                        else
                            importDialog.open()
                    }
                }
            }

            AppText {
                width: parent.width
                height: root.statusMessage.length > 0 ? implicitHeight : 0
                text: root.statusMessage
                color: !uiFixtureMode && libraryViewModel
                       && libraryViewModel.errorMessage.length > 0
                       ? Theme.danger : Theme.textMuted
                font.pixelSize: Theme.captionSize
                visible: root.statusMessage.length > 0
                wrapMode: Text.Wrap
            }

            SectionHeader {
                width: parent.width
                title: root.activeFilter
                detail: (!uiFixtureMode && libraryViewModel
                         && libraryViewModel.loading)
                        ? "正在读取…"
                        : (!uiFixtureMode && libraryViewModel
                           && libraryViewModel.removing)
                          ? "正在移除…"
                          : uiFixtureMode
                            ? root.visibleItems.length + " 个条目"
                            : root.visibleSubjectGroups.length + " 个条目 · "
                              + root.visibleUnassociatedGroups.length
                              + " 个待整理目录 · "
                              + root.visibleItems.length + " 个媒体文件"
            }

            Flow {
                width: parent.width
                height: childrenRect.height
                spacing: 16
                visible: uiFixtureMode && root.visibleItems.length > 0

                Repeater {
                    model: uiFixtureMode ? root.visibleItems : []

                    SubjectCard {
                        subject: modelData
                        onActivated: selected => root.openSubject(selected)
                    }
                }
            }

            Column {
                id: libraryHierarchy
                width: parent.width
                spacing: 16
                visible: !uiFixtureMode && root.visibleItems.length > 0

                Repeater {
                    model: uiFixtureMode ? [] : root.visibleSubjectGroups

                    Rectangle {
                        required property var modelData

                        width: libraryHierarchy.width
                        height: subjectContent.implicitHeight + 32
                        radius: Theme.radiusLarge
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.border

                        Column {
                            id: subjectContent
                            x: 16
                            y: 16
                            width: parent.width - 32
                            spacing: 12

                            RowLayout {
                                width: parent.width

                                Column {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    AppText {
                                        width: parent.width
                                        text: modelData.title
                                        color: Theme.text
                                        font.pixelSize: Theme.headingSize
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    AppText {
                                        width: parent.width
                                        text: modelData.episodeCount
                                              + " 个章节 · "
                                              + modelData.mediaCount
                                              + " 个媒体文件"
                                        color: Theme.textFaint
                                        font.pixelSize: Theme.captionSize
                                    }
                                }

                                AppButton {
                                    text: "编辑元数据"
                                    enabled: !root.actionsBusy()
                                    onClicked: libraryViewModel
                                        .loadLocalMetadata(modelData.subjectId)
                                }

                                AppButton {
                                    text: "删除条目"
                                    enabled: !root.actionsBusy()
                                    onClicked: {
                                        root.pendingLocalMetadataDeletion =
                                            modelData
                                        removeLocalMetadataDialog.open()
                                    }
                                }

                                AppButton {
                                    text: "查看条目详情"
                                    enabled: !root.actionsBusy()
                                    onClicked: root.openSubject({
                                        subjectId: modelData.subjectId,
                                        title: modelData.title,
                                        subtitle: "",
                                        meta: "本地数据库条目",
                                        summary: "",
                                        color: Theme.surfaceRaised
                                    })
                                }
                            }

                            Repeater {
                                model: modelData.episodes

                                Rectangle {
                                    required property var modelData

                                    width: subjectContent.width
                                    height: episodeContent.implicitHeight + 24
                                    radius: Theme.radius
                                    color: Theme.surfaceRaised
                                    border.width: 1
                                    border.color: Theme.border

                                    Column {
                                        id: episodeContent
                                        x: 12
                                        y: 12
                                        width: parent.width - 24
                                        spacing: 10

                                        SectionHeader {
                                            width: parent.width
                                            title: modelData.label
                                            detail: modelData.items.length
                                                    === modelData.totalItemCount
                                                    ? modelData.items.length
                                                      + " 个文件"
                                                    : modelData.items.length
                                                      + " / "
                                                      + modelData.totalItemCount
                                                      + " 个文件"
                                        }

                                        Flow {
                                            width: parent.width
                                            height: childrenRect.height
                                            spacing: 16

                                            Repeater {
                                                model: modelData.items

                                                MediaItemCard {
                                                    media: modelData
                                                    contextAssociation:
                                                        modelData.contextAssociation
                                                    actionEnabled:
                                                        !root.actionsBusy()
                                                    onPlayRequested: selected =>
                                                        libraryViewModel.playMedia(
                                                            selected.id)
                                                    onLinkRequested: selected =>
                                                        root.openAssociation(
                                                            selected)
                                                    onDetailsRequested:
                                                        function(selected,
                                                                 association) {
                                                        root.openSubject({
                                                            subjectId:
                                                                association.subjectId,
                                                            title:
                                                                association.subjectTitle,
                                                            subtitle: "",
                                                            meta:
                                                                "本地数据库条目",
                                                            summary: "",
                                                            color: selected.color
                                                        })
                                                    }
                                                    onUnlinkRequested:
                                                        function(selected,
                                                                 association) {
                                                        libraryViewModel.unlinkMedia(
                                                            selected.id,
                                                            association.episodeId)
                                                    }
                                                    onRemoveRequested: selected =>
                                                        root.confirmRemoval(selected)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 12
                    visible: root.visibleUnassociatedGroups.length > 0

                    SectionHeader {
                        width: parent.width
                        title: "待整理"
                        detail: root.visibleUnassociatedGroups.length
                                + " 个目录"
                    }

                    Repeater {
                        model: root.visibleUnassociatedGroups

                        Rectangle {
                            required property var modelData

                            width: libraryHierarchy.width
                            height: unassociatedContent.implicitHeight + 32
                            radius: Theme.radius
                            color: Theme.surface
                            border.width: 1
                            border.color: Theme.border

                            Column {
                                id: unassociatedContent
                                x: 16
                                y: 16
                                width: parent.width - 32
                                spacing: 12

                                SectionHeader {
                                    width: parent.width
                                    title: modelData.title
                                    detail: modelData.items.length
                                            === modelData.totalItemCount
                                            ? modelData.items.length
                                              + " 个未关联文件"
                                            : modelData.items.length + " / "
                                              + modelData.totalItemCount
                                              + " 个未关联文件"
                                }

                                Flow {
                                    width: parent.width
                                    height: childrenRect.height
                                    spacing: 16

                                    Repeater {
                                        model: modelData.items

                                        MediaItemCard {
                                            media: modelData
                                            actionEnabled:
                                                !root.actionsBusy()
                                            onPlayRequested: selected =>
                                                libraryViewModel.playMedia(
                                                    selected.id)
                                            onLinkRequested: selected =>
                                                root.openAssociation(selected)
                                            onRemoveRequested: selected =>
                                                root.confirmRemoval(selected)
                                        }
                                    }
                                }
                            }
                        }
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
                visible: root.visibleItems.length === 0
                         && (uiFixtureMode || !libraryViewModel
                             || !libraryViewModel.loading)

                Column {
                    anchors.centerIn: parent
                    spacing: 8

                    AppText {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: searchField.text.length > 0
                              ? "没有匹配的媒体文件"
                              : (uiFixtureMode ? "没有匹配的条目"
                                               : "媒体库还是空的")
                        color: Theme.text
                        font.pixelSize: Theme.bodySize
                    }

                    AppText {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: searchField.text.length > 0
                              ? "换个关键词或筛选条件试试"
                              : (uiFixtureMode ? "换个筛选条件试试"
                                               : "点击“导入媒体”选择本地文件")
                        color: Theme.textMuted
                        font.pixelSize: Theme.captionSize
                    }
                }
            }

            Item { width: 1; height: 16 }
        }
    }

    Component.onCompleted: {
        if (!uiFixtureMode && libraryViewModel)
            libraryViewModel.refresh()
    }
}
