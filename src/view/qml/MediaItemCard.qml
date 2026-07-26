import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    property var media
    property var contextAssociation: null
    property bool actionEnabled: true
    readonly property var detailsAssociation: contextAssociation
            || (media && media.associationCount > 0
                ? media.associations[0] : null)
    readonly property var unlinkAssociation: contextAssociation
            || (media && media.associationCount === 1
                ? media.associations[0] : null)

    signal removeRequested(var mediaItem)
    signal linkRequested(var mediaItem)
    signal unlinkRequested(var mediaItem, var association)
    signal playRequested(var mediaItem)
    signal detailsRequested(var mediaItem, var association)

    width: 270
    height: 106
    radius: Theme.radius
    color: cardMouse.containsMouse ? Theme.surfaceHover : Theme.surface
    border.width: 1
    border.color: Theme.border

    Rectangle {
        id: marker
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 8
        width: 58
        radius: Theme.radiusSmall
        color: root.media && root.media.color
               ? root.media.color : Theme.surfaceRaised

        AppText {
            anchors.centerIn: parent
            text: root.media && root.media.title
                  ? root.media.title.slice(0, 1).toUpperCase() : "M"
            color: "#dfe3e6"
            opacity: 0.82
            font.pixelSize: 26
            font.weight: Font.DemiBold
        }
    }

    Column {
        anchors.left: marker.right
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 6

        AppText {
            width: parent.width
            text: root.media ? root.media.title : ""
            color: Theme.text
            font.pixelSize: Theme.bodySize
            font.weight: Font.DemiBold
            elide: Text.ElideMiddle
        }

        AppText {
            width: parent.width
            text: root.media ? root.media.subtitle : ""
            color: Theme.textMuted
            font.pixelSize: Theme.captionSize
            elide: Text.ElideMiddle
        }

        Rectangle {
            width: statusLabel.implicitWidth + 12
            height: 22
            radius: 5
            color: Theme.surfaceRaised

            AppText {
                id: statusLabel
                anchors.centerIn: parent
                text: root.media ? root.media.status : "未关联"
                color: Theme.textMuted
                font.pixelSize: 11
            }
        }
    }

    Menu {
        id: contextMenu
        implicitWidth: 190
        padding: 4

        background: Rectangle {
            implicitWidth: 190
            implicitHeight: 8 + 38 * (3
                            + (root.detailsAssociation ? 1 : 0)
                            + (root.unlinkAssociation ? 1 : 0))
            radius: Theme.radiusSmall
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.border
        }

        MenuItem {
            id: playAction
            text: "播放"
            enabled: root.actionEnabled && root.media
            implicitWidth: 182
            implicitHeight: 38
            leftPadding: 12
            rightPadding: 12
            contentItem: AppText {
                text: playAction.text
                color: playAction.enabled ? Theme.text : Theme.textFaint
                font.pixelSize: Theme.bodySize
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.radiusSmall
                color: playAction.highlighted || playAction.hovered
                       ? Theme.surfaceHover : "transparent"
            }
            onTriggered: root.playRequested(root.media)
        }

        MenuItem {
            id: linkAction
            text: root.media && root.media.associationCount > 0
                  ? "添加或管理关联" : "关联章节"
            enabled: root.actionEnabled && root.media
            implicitWidth: 182
            implicitHeight: 38
            leftPadding: 12
            rightPadding: 12
            contentItem: AppText {
                text: linkAction.text
                color: linkAction.enabled ? Theme.text : Theme.textFaint
                font.pixelSize: Theme.bodySize
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.radiusSmall
                color: linkAction.highlighted || linkAction.hovered
                       ? Theme.surfaceHover : "transparent"
            }
            onTriggered: root.linkRequested(root.media)
        }

        MenuItem {
            id: detailsAction
            text: root.contextAssociation
                  ? "查看条目详情"
                  : root.media && root.media.associationCount > 1
                  ? "查看首个关联条目" : "查看条目详情"
            visible: root.detailsAssociation !== null
            enabled: root.actionEnabled && visible
            implicitWidth: 182
            implicitHeight: visible ? 38 : 0
            leftPadding: 12
            rightPadding: 12
            contentItem: AppText {
                text: detailsAction.text
                color: detailsAction.enabled ? Theme.text : Theme.textFaint
                font.pixelSize: Theme.bodySize
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.radiusSmall
                color: detailsAction.highlighted || detailsAction.hovered
                       ? Theme.surfaceHover : "transparent"
            }
            onTriggered: root.detailsRequested(root.media,
                                                root.detailsAssociation)
        }

        MenuItem {
            id: unlinkAction
            text: root.contextAssociation
                  ? "解除该章节关联" : "解除当前关联"
            visible: root.unlinkAssociation !== null
            enabled: root.actionEnabled && visible
            implicitWidth: 182
            implicitHeight: visible ? 38 : 0
            leftPadding: 12
            rightPadding: 12
            contentItem: AppText {
                text: unlinkAction.text
                color: unlinkAction.enabled ? Theme.warning : Theme.textFaint
                font.pixelSize: Theme.bodySize
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.radiusSmall
                color: unlinkAction.highlighted || unlinkAction.hovered
                       ? Theme.surfaceHover : "transparent"
            }
            onTriggered: root.unlinkRequested(root.media,
                                               root.unlinkAssociation)
        }

        MenuItem {
            id: removeAction
            text: "从媒体库移除"
            enabled: root.actionEnabled && root.media
            implicitWidth: 182
            implicitHeight: 38
            leftPadding: 12
            rightPadding: 12

            contentItem: AppText {
                text: removeAction.text
                color: removeAction.enabled
                       ? Theme.danger : Theme.textFaint
                font.pixelSize: Theme.bodySize
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }

            background: Rectangle {
                radius: Theme.radiusSmall
                color: removeAction.highlighted || removeAction.hovered
                       ? Theme.surfaceHover : "transparent"
            }

            onTriggered: root.removeRequested(root.media)
        }
    }

    MouseArea {
        id: cardMouse
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton)
                contextMenu.popup(mouse.x, mouse.y)
        }
        onDoubleClicked: function(mouse) {
            if (mouse.button === Qt.LeftButton && root.actionEnabled)
                root.playRequested(root.media)
        }
    }
}
