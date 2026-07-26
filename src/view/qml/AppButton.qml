import QtQuick
import QtQuick.Controls

Button {
    id: control

    property bool primary: false
    property bool quiet: false

    hoverEnabled: true
    implicitHeight: 38
    implicitWidth: Math.max(88, implicitContentWidth + 28)
    leftPadding: 14
    rightPadding: 14

    contentItem: AppText {
        text: control.text
        color: !control.enabled
               ? Theme.textFaint
               : (control.primary ? Theme.accentText : Theme.text)
        font.pixelSize: Theme.bodySize
        font.weight: control.primary ? Font.DemiBold : Font.Normal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        border.width: control.quiet ? 0 : 1
        border.color: !control.enabled
                      ? Theme.border
                      : (control.primary ? Theme.accent : Theme.border)
        color: {
            if (!control.enabled)
                return Theme.surface
            if (control.down)
                return control.primary ? Theme.accentHover : Theme.surfaceHover
            if (control.hovered)
                return control.primary ? Theme.accentHover : Theme.surfaceRaised
            if (control.primary)
                return Theme.accent
            return control.quiet ? "transparent" : Theme.surface
        }
    }
}
