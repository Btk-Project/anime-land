import QtQuick

Rectangle {
    id: root

    property url source
    property string title: ""
    property color fallbackColor: Theme.surfaceRaised
    property real decodeOverscan: 1.25
    readonly property bool ready: image.status === Image.Ready

    radius: Theme.radiusSmall
    color: fallbackColor
    clip: true

    Image {
        id: image
        anchors.fill: parent
        source: root.source
        sourceSize: Qt.size(
            Math.ceil(width * Screen.devicePixelRatio * root.decodeOverscan),
            Math.ceil(height * Screen.devicePixelRatio * root.decodeOverscan))
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        smooth: true
        mipmap: false
        autoTransform: true
        visible: status === Image.Ready
    }

    AppText {
        anchors.centerIn: parent
        text: root.title.length > 0 ? root.title.slice(0, 1) : "?"
        visible: !root.ready
        color: "#e2e5e7"
        opacity: 0.75
        font.pixelSize: Math.min(64, Math.max(28, root.width * 0.34))
        font.weight: Font.Light
    }
}
