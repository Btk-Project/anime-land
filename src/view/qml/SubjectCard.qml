import QtQuick

Rectangle {
    id: root

    property var subject
    signal activated(var subject)

    width: 172
    height: 286
    radius: Theme.radius
    color: mouseArea.containsMouse ? Theme.surfaceRaised : Theme.surface
    border.width: 1
    border.color: mouseArea.containsMouse ? Theme.accent : Theme.border

    Behavior on border.color { ColorAnimation { duration: 120 } }
    Behavior on color { ColorAnimation { duration: 120 } }

    Rectangle {
        id: cover
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        height: 208
        radius: Theme.radiusSmall
        color: root.subject && root.subject.color
               ? root.subject.color : Theme.surfaceRaised

        AppText {
            anchors.centerIn: parent
            text: root.subject && root.subject.title
                  ? root.subject.title.slice(0, 1) : "A"
            color: "#dfe3e6"
            opacity: 0.78
            font.pixelSize: 54
            font.weight: Font.Light
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 4
            color: "#343a40"
            visible: root.subject && root.subject.progress > 0

            Rectangle {
                height: parent.height
                width: parent.width * Math.min(1, root.subject.progress)
                color: Theme.accent
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 8
            width: statusLabel.implicitWidth + 12
            height: 24
            radius: 5
            color: "#b3111315"

            AppText {
                id: statusLabel
                anchors.centerIn: parent
                text: root.subject ? root.subject.status : ""
                color: Theme.text
                font.pixelSize: 11
            }
        }
    }

    AppText {
        id: titleLabel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: cover.bottom
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: 10
        text: root.subject ? root.subject.title : ""
        color: Theme.text
        font.pixelSize: Theme.bodySize
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }

    AppText {
        anchors.left: titleLabel.left
        anchors.right: titleLabel.right
        anchors.top: titleLabel.bottom
        anchors.topMargin: 5
        text: root.subject ? root.subject.meta : ""
        color: Theme.textMuted
        font.pixelSize: Theme.captionSize
        elide: Text.ElideRight
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.activated(root.subject)
    }
}
