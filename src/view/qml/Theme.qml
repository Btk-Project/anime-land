pragma Singleton

import QtQuick

QtObject {
    property string preference: "system"
    property bool systemDark: true
    readonly property bool dark: preference === "dark"
                                 || (preference === "system" && systemDark)

    readonly property color background: dark ? "#111315" : "#f4f6f8"
    readonly property color sidebar: dark ? "#151719" : "#eef1f4"
    readonly property color surface: dark ? "#191c1f" : "#ffffff"
    readonly property color surfaceRaised: dark ? "#202428" : "#edf0f3"
    readonly property color surfaceHover: dark ? "#262a2f" : "#e3e8ec"
    readonly property color border: dark ? "#2d3237" : "#d5dbe0"

    readonly property color text: dark ? "#eceff1" : "#202428"
    readonly property color textMuted: dark ? "#9aa1a8" : "#626d76"
    readonly property color textFaint: dark ? "#6f767d" : "#87919a"

    readonly property color accent: dark ? "#7d8893" : "#657686"
    readonly property color accentHover: dark ? "#919ca7" : "#526676"
    readonly property color accentText: dark ? "#111315" : "#ffffff"
    readonly property color success: dark ? "#84968a" : "#557462"
    readonly property color warning: dark ? "#a39882" : "#806d43"
    readonly property color danger: dark ? "#a77f7f" : "#9a4f4f"

    readonly property int radiusSmall: 6
    readonly property int radius: 9
    readonly property int radiusLarge: 12
    readonly property int spacingSmall: 8
    readonly property int spacing: 16
    readonly property int spacingLarge: 24
    readonly property int pageMargin: 32

    readonly property int titleSize: 26
    readonly property int headingSize: 18
    readonly property int bodySize: 14
    readonly property int captionSize: 12
}
