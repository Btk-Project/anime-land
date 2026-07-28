import QtQuick

TextEdit {
    id: control

    readOnly: true
    selectByMouse: true
    persistentSelection: true
    textFormat: TextEdit.PlainText
    color: Theme.text
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    font.family: Qt.platform.os === "windows"
                 ? "Microsoft YaHei UI" : "sans-serif"
    activeFocusOnTab: true
    cursorVisible: false
}
