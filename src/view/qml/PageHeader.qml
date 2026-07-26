import QtQuick

Item {
    id: root

    property string title: ""
    property string subtitle: ""

    implicitHeight: subtitle.length > 0 ? 72 : 48

    AppText {
        id: titleLabel
        anchors.left: parent.left
        anchors.top: parent.top
        text: root.title
        color: Theme.text
        font.pixelSize: Theme.titleSize
        font.weight: Font.DemiBold
    }

    AppText {
        anchors.left: parent.left
        anchors.top: titleLabel.bottom
        anchors.topMargin: 7
        text: root.subtitle
        color: Theme.textMuted
        font.pixelSize: Theme.bodySize
        visible: root.subtitle.length > 0
    }
}
