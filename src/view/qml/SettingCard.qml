import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    property string title: ""
    property string description: ""
    property string actionText: "管理"
    signal actionRequested()

    implicitHeight: 88
    radius: Theme.radius
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 20

        Column {
            Layout.fillWidth: true
            spacing: 7

            AppText {
                width: parent.width
                text: root.title
                color: Theme.text
                font.pixelSize: Theme.bodySize
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            AppText {
                width: parent.width
                text: root.description
                color: Theme.textMuted
                font.pixelSize: Theme.captionSize
                elide: Text.ElideRight
            }
        }

        AppButton {
            text: root.actionText
            onClicked: root.actionRequested()
        }
    }
}
