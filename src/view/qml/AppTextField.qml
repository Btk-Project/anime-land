import QtQuick
import QtQuick.Controls

TextField {
    id: control

    color: Theme.text
    placeholderTextColor: Theme.textFaint
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    font.family: Qt.platform.os === "windows"
                 ? "Microsoft YaHei UI" : "sans-serif"
    font.pixelSize: Theme.bodySize
    leftPadding: 13
    rightPadding: 13
    selectByMouse: true
    activeFocusOnTab: true
    inputMethodHints: Qt.ImhNone

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surface
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : Theme.border
    }
}
