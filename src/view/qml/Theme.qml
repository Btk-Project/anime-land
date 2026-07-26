pragma Singleton

import QtQuick

QtObject {
    readonly property color background: "#111315"
    readonly property color sidebar: "#151719"
    readonly property color surface: "#191c1f"
    readonly property color surfaceRaised: "#202428"
    readonly property color surfaceHover: "#262a2f"
    readonly property color border: "#2d3237"

    readonly property color text: "#eceff1"
    readonly property color textMuted: "#9aa1a8"
    readonly property color textFaint: "#6f767d"

    readonly property color accent: "#7d8893"
    readonly property color accentHover: "#919ca7"
    readonly property color accentText: "#111315"
    readonly property color success: "#84968a"
    readonly property color warning: "#a39882"
    readonly property color danger: "#a77f7f"

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
