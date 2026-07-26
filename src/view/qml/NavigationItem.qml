import QtQuick
import QtQuick.Controls

Button {
    id: control

    property bool selected: false
    property string marker: ""

    hoverEnabled: true
    implicitHeight: 44
    leftPadding: 12
    rightPadding: 12

    contentItem: Row {
        spacing: 12

        AppText {
            width: 18
            anchors.verticalCenter: parent.verticalCenter
            text: control.marker
            color: control.selected ? Theme.text : Theme.textMuted
            font.pixelSize: 14
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
        }

        AppText {
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            color: control.selected ? Theme.text : Theme.textMuted
            font.pixelSize: Theme.bodySize
            font.weight: control.selected ? Font.DemiBold : Font.Normal
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: control.selected
               ? Theme.surfaceRaised
               : control.hovered ? Theme.surface : "transparent"

        Rectangle {
            width: 3
            height: 20
            radius: 2
            anchors.left: parent.left
            anchors.leftMargin: 1
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.accent
            visible: control.selected
        }
    }
}
