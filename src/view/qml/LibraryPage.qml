import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    signal openSubject(var subject)

    property string activeFilter: "全部"
    property string fixtureNotice: ""
    property var pendingRemoval: null
    property var pendingAssociation: null
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
        associationSearch.text = media.resourceTitle || ""
        libraryViewModel.clearAssociationPicker()
        associationDialog.open()
        associationSearch.forceActiveFocus()
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
        id: associationDialog
        anchors.centerIn: parent
        width: Math.min(820, root.width - Theme.pageMargin * 2)
        height: Math.min(650, root.height - Theme.pageMargin * 2)
        modal: true
        title: "关联 Bangumi 章节"
        standardButtons: Dialog.Cancel
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
            if (libraryViewModel)
                libraryViewModel.clearAssociationPicker()
        }

        ColumnLayout {
            width: associationDialog.availableWidth
            height: associationDialog.availableHeight
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

                TextField {
                    id: associationSearch
                    Layout.fillWidth: true
                    height: 40
                    placeholderText: "搜索动画名称"
                    color: Theme.text
                    placeholderTextColor: Theme.textFaint
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.accentText
                    leftPadding: 13
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.surface
                        border.width: 1
                        border.color: associationSearch.activeFocus
                                      ? Theme.accent : Theme.border
                    }
                    onAccepted: {
                        if (!root.actionsBusy())
                            libraryViewModel.searchAssociationSubjects(text)
                    }
                }

                AppButton {
                    text: libraryViewModel && libraryViewModel.associating
                          ? "正在读取…" : "搜索"
                    primary: true
                    enabled: !root.actionsBusy()
                    onClicked: libraryViewModel.searchAssociationSubjects(
                                   associationSearch.text)
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

                        ListView {
                            id: subjectResults
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 6
                            model: libraryViewModel
                                   ? libraryViewModel.associationSubjects : []

                            delegate: Rectangle {
                                required property var modelData
                                width: ListView.view.width
                                height: 66
                                radius: Theme.radiusSmall
                                color: subjectMouse.containsMouse
                                       ? Theme.surfaceHover
                                       : Theme.surfaceRaised
                                border.width: 1
                                border.color: Theme.border

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
                                    onClicked: libraryViewModel
                                                   .selectAssociationSubject(
                                                       modelData.bangumiId)
                                }
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
                        spacing: 8

                        AppText {
                            text: "章节"
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            font.weight: Font.DemiBold
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 6
                            model: libraryViewModel
                                   ? libraryViewModel.associationEpisodes : []

                            delegate: Rectangle {
                                required property var modelData
                                width: ListView.view.width
                                height: 58
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
                    }
                }
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
                title: "媒体库"
                subtitle: uiFixtureMode
                          ? "浏览本地条目、观看进度和媒体关联状态。"
                          : "已关联媒体按条目与章节整理，未关联文件保留在待整理区。"
            }

            Row {
                width: parent.width
                spacing: 10

                TextField {
                    id: searchField
                    width: Math.min(300, parent.width - 390)
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
