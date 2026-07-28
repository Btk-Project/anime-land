import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AnimeLand.Playback 1.0

Item {
    id: root
    objectName: "playerPage"

    property var subject: uiFixtureMode ? FixtureData.subjects[0] : ({})
    property bool fullScreen: false
    readonly property bool livePlayback: !uiFixtureMode && playbackController
    readonly property var episodeModel: uiFixtureMode
            ? FixtureData.episodes
            : (subject && subject.episodes ? subject.episodes : [])
    readonly property string titleText: livePlayback
                                                ? playbackController.mediaTitle
                                                : (subject.title || "Fixture Player")
    property real displayedPositionMs: 0
    property bool seekPending: false
    property real pendingSeekPositionMs: 0
    property bool controlsVisible: true
    property real lastPointerX: -1
    property real lastPointerY: -1
    readonly property bool chapterDrawerHovered: edgeHover.hovered
                                                   || drawerHover.hovered
    readonly property bool chapterDrawerOpen: chapterDrawerHovered
                                                || drawerCloseDelay.running
    signal backRequested()
    signal fullScreenRequested(bool enabled)
    signal playEpisodeRequested(var episode)

    onFullScreenChanged: revealControls()

    onChapterDrawerHoveredChanged: {
        if (chapterDrawerHovered)
            drawerCloseDelay.stop()
        else
            drawerCloseDelay.restart()
    }

    function requestBack() {
        if (fullScreen)
            fullScreenRequested(false)
        else
            backRequested()
    }

    function revealControls() {
        controlsVisible = true
        controlsHideDelay.restart()
    }

    function notePointerPosition(x, y) {
        if (lastPointerX >= 0
                && Math.abs(x - lastPointerX) < 0.5
                && Math.abs(y - lastPointerY) < 0.5)
            return
        lastPointerX = x
        lastPointerY = y
        revealControls()
    }

    function formatTime(milliseconds) {
        const total = Math.max(0, Math.floor(milliseconds / 1000))
        const hours = Math.floor(total / 3600)
        const minutes = Math.floor((total % 3600) / 60)
        const seconds = total % 60
        if (hours > 0)
            return hours + ":" + String(minutes).padStart(2, "0")
                   + ":" + String(seconds).padStart(2, "0")
        return String(minutes).padStart(2, "0")
               + ":" + String(seconds).padStart(2, "0")
    }

    function syncPlaybackPosition() {
        if (!livePlayback || progressSlider.pressed)
            return

        if (seekPending) {
            const closeToTarget = Math.abs(playbackController.positionMs
                                           - pendingSeekPositionMs) <= 750
            const seekAborted = playbackController.stateName === "error"
                                || playbackController.stateName === "idle"
            if (!closeToTarget && !seekAborted)
                return
            seekPending = false
            seekSyncTimeout.stop()
        }

        displayedPositionMs = playbackController.positionMs
    }

    Component.onCompleted: {
        if (livePlayback)
            displayedPositionMs = playbackController.positionMs
        else
            displayedPositionMs = subject.progress || 0
    }

    Connections {
        target: root.livePlayback ? playbackController : null

        function onSnapshotChanged() {
            root.syncPlaybackPosition()
        }

        function onMediaChanged() {
            root.seekPending = false
            seekSyncTimeout.stop()
            root.displayedPositionMs = 0
        }
    }

    Timer {
        id: seekSyncTimeout
        interval: 2500
        repeat: false
        onTriggered: {
            root.seekPending = false
            root.syncPlaybackPosition()
        }
    }

    Timer {
        id: drawerCloseDelay
        interval: 350
        repeat: false
    }

    Timer {
        id: controlsHideDelay
        interval: 2500
        repeat: false
        running: true
        onTriggered: {
            if (progressSlider.pressed)
                restart()
            else
                root.controlsVisible = false
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.fullScreen ? 0 : 60
                visible: !root.fullScreen
                color: Theme.sidebar

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 20
                    spacing: 14

                    AppButton {
                        text: "返回"
                        quiet: true
                        onClicked: root.requestBack()
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: 3

                        AppText {
                            width: parent.width
                            text: root.titleText
                            color: Theme.text
                            font.pixelSize: Theme.bodySize
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        AppText {
                            width: parent.width
                            text: root.livePlayback
                                  ? playbackController.stateName
                                  : (root.subject.episode || "画面输出 Fixture")
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Rectangle {
                id: viewport
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#050607"
                clip: true

                HoverHandler {
                    id: controlRevealHover
                    onHoveredChanged: {
                        root.lastPointerX = -1
                        root.lastPointerY = -1
                        if (hovered)
                            root.notePointerPosition(point.position.x,
                                                     point.position.y)
                    }
                    onPointChanged: {
                        if (hovered)
                            root.notePointerPosition(point.position.x,
                                                     point.position.y)
                    }
                }

                VideoOutputItem {
                    id: videoOutput
                    source: root.livePlayback ? playbackVideoSurface : null
                    anchors.centerIn: parent
                    readonly property real frameAspect:
                        frameSize.height > 0 ? frameSize.width / frameSize.height
                                             : 16 / 9
                    readonly property real viewportAspect:
                        viewport.height > 0 ? viewport.width / viewport.height
                                            : frameAspect
                    width: viewportAspect > frameAspect
                           ? viewport.height * frameAspect : viewport.width
                    height: viewportAspect > frameAspect
                            ? viewport.height : viewport.width / frameAspect
                    visible: source && source.hasFrame
                }

                Column {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 48, 520)
                    spacing: 10
                    visible: !videoOutput.visible

                    AppText {
                        width: parent.width
                        text: root.livePlayback
                              && playbackController.errorMessage.length > 0
                              ? playbackController.errorMessage
                              : (root.livePlayback
                                 ? "正在准备视频画面…"
                                 : "VideoOutputItem · QRhi RGBA")
                        color: root.livePlayback
                               && playbackController.errorMessage.length > 0
                               ? Theme.danger : Theme.textFaint
                        font.pixelSize: Theme.bodySize
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                }

                Rectangle {
                    id: playbackControls
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 96
                    color: Theme.dark ? "#e6151719" : "#eff8fafb"
                    opacity: root.controlsVisible ? 1 : 0
                    enabled: root.controlsVisible

                    Behavior on opacity {
                        NumberAnimation { duration: 160 }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 20
                        anchors.rightMargin: 20
                        anchors.topMargin: 10
                        anchors.bottomMargin: 10
                        spacing: 8

                        Slider {
                            id: progressSlider
                            Layout.fillWidth: true
                            hoverEnabled: true
                            from: 0
                            to: root.livePlayback
                                ? Math.max(1, playbackController.durationMs)
                                : 1
                            value: root.displayedPositionMs
                            enabled: root.livePlayback
                                     && playbackController.durationMs > 0

                            onMoved: root.displayedPositionMs = value

                            onPressedChanged: {
                                if (!root.livePlayback)
                                    return
                                if (pressed) {
                                    root.revealControls()
                                    root.seekPending = false
                                    seekSyncTimeout.stop()
                                    return
                                }

                                root.pendingSeekPositionMs = Math.round(value)
                                root.displayedPositionMs
                                    = root.pendingSeekPositionMs
                                root.seekPending = true
                                seekSyncTimeout.restart()
                                playbackController.seek(
                                    root.pendingSeekPositionMs)
                            }

                            background: Rectangle {
                                x: progressSlider.leftPadding
                                y: progressSlider.topPadding
                                   + progressSlider.availableHeight / 2
                                   - height / 2
                                width: progressSlider.availableWidth
                                height: 6
                                radius: 3
                                color: progressSlider.enabled
                                       ? (Theme.dark
                                          ? "#59646d" : "#b5bec5")
                                       : (Theme.dark
                                          ? "#343a3f" : "#d7dde1")

                                Rectangle {
                                    width: progressSlider.visualPosition
                                           * parent.width
                                    height: parent.height
                                    radius: parent.radius
                                    color: progressSlider.enabled
                                           ? (Theme.dark
                                              ? "#9ed9fb" : "#477f9f")
                                           : (Theme.dark
                                              ? "#626b72" : "#aeb7bd")
                                }
                            }

                            handle: Rectangle {
                                x: progressSlider.leftPadding
                                   + progressSlider.visualPosition
                                   * (progressSlider.availableWidth - width)
                                y: progressSlider.topPadding
                                   + progressSlider.availableHeight / 2
                                   - height / 2
                                width: progressSlider.pressed
                                       || progressSlider.hovered ? 22 : 18
                                height: width
                                radius: width / 2
                                color: progressSlider.enabled
                                       ? (Theme.dark
                                          ? "#f7fbfe" : "#ffffff")
                                       : (Theme.dark
                                          ? "#737b81" : "#c1c8cd")
                                border.width: progressSlider.pressed
                                              || progressSlider.activeFocus
                                              ? 3 : 2
                                border.color: progressSlider.pressed
                                              || progressSlider.activeFocus
                                              ? (Theme.dark
                                                 ? "#69c7fb" : "#356f90")
                                              : (Theme.dark
                                                 ? "#182026" : "#46545e")

                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 6
                                    height: 6
                                    radius: 3
                                    color: progressSlider.enabled
                                           ? (Theme.dark
                                              ? "#278ec8" : "#356f90")
                                           : (Theme.dark
                                              ? "#454b50" : "#89949b")
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            AppButton {
                                text: root.livePlayback
                                      && playbackController.playing
                                      ? "暂停" : "播放"
                                quiet: true
                                onDark: Theme.dark
                                enabled: root.livePlayback
                                         && playbackController.stateName
                                            !== "opening"
                                         && playbackController.stateName
                                            !== "stopping"
                                onClicked: playbackController.togglePlayback()
                            }

                            AppButton {
                                text: "后退 10s"
                                quiet: true
                                onDark: Theme.dark
                                enabled: root.livePlayback
                                onClicked: playbackController.seek(
                                    playbackController.positionMs - 10000)
                            }

                            AppButton {
                                text: "前进 10s"
                                quiet: true
                                onDark: Theme.dark
                                enabled: root.livePlayback
                                onClicked: playbackController.seek(
                                    playbackController.positionMs + 10000)
                            }

                            AppText {
                                text: root.livePlayback
                                      ? root.formatTime(
                                            root.displayedPositionMs)
                                        + " / "
                                        + root.formatTime(
                                            playbackController.durationMs)
                                      : "00:00 / 00:01"
                                color: Theme.dark ? "#b7bec5" : Theme.textMuted
                                font.pixelSize: Theme.captionSize
                            }

                            Item { Layout.fillWidth: true }

                            AppText {
                                visible: root.livePlayback
                                text: videoOutput.frameSize.width > 0
                                      ? videoOutput.frameSize.width + " × "
                                        + videoOutput.frameSize.height
                                      : "等待首帧"
                                color: Theme.dark ? "#8f989f" : Theme.textFaint
                                font.pixelSize: Theme.captionSize
                            }

                            AppButton {
                                text: root.fullScreen ? "退出全屏" : "全屏"
                                quiet: true
                                onDark: Theme.dark
                                onClicked: root.fullScreenRequested(
                                               !root.fullScreen)
                            }
                        }
                    }
                }

                Rectangle {
                    id: chapterDrawer
                    z: 20
                    width: Math.min(320, Math.max(260,
                                                  viewport.width * 0.28))
                    height: parent.height
                    x: root.chapterDrawerOpen
                       ? parent.width - width : parent.width
                    color: Theme.dark ? "#f315181b" : "#f7f4f6f8"
                    border.width: 1
                    border.color: Theme.border

                    Behavior on x {
                        NumberAnimation {
                            duration: 170
                            easing.type: Easing.OutCubic
                        }
                    }

                    HoverHandler { id: drawerHover }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        anchors.topMargin: 16
                        anchors.bottomMargin: 14
                        spacing: 10

                        AppText {
                            text: "章节队列"
                            color: Theme.text
                            font.pixelSize: Theme.headingSize
                            font.weight: Font.DemiBold
                        }

                        AppText {
                            text: root.episodeModel.length > 0
                                  ? "已关联媒体优先 · 移出右侧自动收起"
                                  : "当前媒体没有可用的章节队列"
                            color: Theme.textMuted
                            font.pixelSize: Theme.captionSize
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }

                        ListView {
                            id: episodeQueue
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 7
                            model: root.episodeModel

                            ScrollBar.vertical: ScrollBar {}

                            delegate: Rectangle {
                                id: episodeDelegate
                                required property var modelData
                                required property int index

                                width: ListView.view.width
                                height: 58
                                radius: 6
                                color: episodeMouse.containsMouse
                                       ? Theme.surfaceHover : Theme.surface
                                border.width: 1
                                border.color: modelData.progress > 0
                                              && modelData.progress < 1
                                              ? (Theme.dark
                                                 ? "#81cffa" : "#477f9f")
                                              : Theme.border

                                Column {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 5

                                    AppText {
                                        width: parent.width
                                        text: modelData.number + "  "
                                              + modelData.title
                                        color: modelData.linked
                                               ? Theme.text : Theme.textFaint
                                        font.pixelSize: Theme.captionSize
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    AppText {
                                        width: parent.width
                                        text: modelData.source
                                        color: modelData.linked
                                               ? Theme.textMuted : Theme.warning
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }

                                MouseArea {
                                    id: episodeMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: modelData.linked
                                                 && !uiFixtureMode
                                                 ? Qt.PointingHandCursor
                                                 : Qt.ArrowCursor
                                    onClicked: {
                                        if (modelData.linked
                                                && !uiFixtureMode)
                                            root.playEpisodeRequested(modelData)
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    z: 30
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: edgeHover.hovered ? 9 : 5
                    height: 64
                    radius: 4
                    color: edgeHover.hovered
                           ? (Theme.dark ? "#b8e5ff" : Theme.accentHover)
                           : (Theme.dark ? "#71808a" : Theme.accent)
                    opacity: root.chapterDrawerOpen ? 0 : 0.8

                    Behavior on width { NumberAnimation { duration: 100 } }
                    Behavior on opacity { NumberAnimation { duration: 100 } }
                }

                Item {
                    z: 31
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    width: 14

                    HoverHandler { id: edgeHover }
                }
            }
        }
    }

    StackView.onDeactivated: {
        if (root.livePlayback)
            playbackController.stop()
        if (root.fullScreen)
            root.fullScreenRequested(false)
    }

    Shortcut {
        sequence: "Space"
        enabled: root.livePlayback
        onActivated: {
            root.revealControls()
            playbackController.togglePlayback()
        }
    }

    Shortcut {
        sequence: "F11"
        autoRepeat: false
        onActivated: root.fullScreenRequested(!root.fullScreen)
    }

    Shortcut {
        sequence: "F"
        autoRepeat: false
        onActivated: root.fullScreenRequested(!root.fullScreen)
    }

    Shortcut {
        sequence: StandardKey.Cancel
        onActivated: root.requestBack()
    }
}
