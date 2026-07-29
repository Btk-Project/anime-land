import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    property var episode
    signal primaryAction(var episode)
    signal onlineAction(var episode)

    implicitHeight: 66
    radius: Theme.radius
    color: rowMouse.containsMouse ? Theme.surfaceRaised : Theme.surface
    border.width: 1
    border.color: Theme.border

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 12
        spacing: 14

        SelectableText {
            Layout.preferredWidth: 30
            text: root.episode ? root.episode.number : ""
            color: Theme.textFaint
            font.pixelSize: Theme.captionSize
            font.weight: Font.DemiBold
            wrapMode: TextEdit.NoWrap
            clip: true
            Layout.alignment: Qt.AlignVCenter
        }

        Column {
            Layout.fillWidth: true
            spacing: 5

            SelectableText {
                width: parent.width
                text: root.episode ? root.episode.title : ""
                color: Theme.text
                font.pixelSize: Theme.bodySize
                font.weight: Font.DemiBold
                wrapMode: TextEdit.NoWrap
                clip: true
                height: implicitHeight
            }

            SelectableText {
                width: parent.width
                text: root.episode ? root.episode.source : ""
                color: root.episode && root.episode.linked
                       ? Theme.textMuted : Theme.warning
                font.pixelSize: Theme.captionSize
                wrapMode: TextEdit.NoWrap
                clip: true
                height: implicitHeight
            }
        }

        SelectableText {
            Layout.preferredWidth: 52
            text: root.episode ? root.episode.duration : ""
            color: Theme.textFaint
            font.pixelSize: Theme.captionSize
            wrapMode: TextEdit.NoWrap
            clip: true
            Layout.alignment: Qt.AlignVCenter
        }

        Rectangle {
            Layout.preferredWidth: 88
            Layout.preferredHeight: 3
            radius: 2
            color: Theme.border

            Rectangle {
                width: parent.width * (root.episode ? root.episode.progress : 0)
                height: parent.height
                radius: parent.radius
                color: Theme.accent
            }
        }

        AppButton {
            Layout.preferredWidth: 78
            text: root.episode && root.episode.linked
                  ? (root.episode.progress > 0 && root.episode.progress < 1
                     ? "继续" : "播放")
                  : "关联"
            primary: root.episode && root.episode.progress > 0
                     && root.episode.progress < 1
            onClicked: root.primaryAction(root.episode)
        }

        AppButton {
            Layout.preferredWidth: 78
            text: "在线"
            onClicked: root.onlineAction(root.episode)
        }
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }
}
