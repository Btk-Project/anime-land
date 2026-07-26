import QtQuick

Item {
    id: root

    property string title: ""
    property string detail: ""

    implicitHeight: 30

    AppText {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        text: root.title
        color: Theme.text
        font.pixelSize: Theme.headingSize
        font.weight: Font.DemiBold
    }

    AppText {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: root.detail
        color: Theme.textFaint
        font.pixelSize: Theme.captionSize
        visible: root.detail.length > 0
    }
}
