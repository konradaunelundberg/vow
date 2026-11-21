import QtQuick

// A flat button in the Omarchy manner: a thin outline that fills with the
// accent when it is the primary action.
Rectangle {
    id: root

    property string text: ""
    property string glyph: ""
    property bool primary: false
    property bool danger: false
    property bool enabledState: true
    property bool compact: false
    signal clicked()

    readonly property color tone: danger ? theme.danger : primary ? theme.accent : theme.foreground

    implicitWidth: row.implicitWidth + (compact ? 16 : 24)
    implicitHeight: compact ? 26 : 32
    radius: Math.min(4, theme.radius)
    opacity: enabledState ? 1 : 0.4
    color: primary && enabledState ? (mouse.pressed ? Qt.darker(theme.accent, 1.2) : theme.accent)
         : mouse.containsMouse && enabledState ? Qt.rgba(tone.r, tone.g, tone.b, 0.12) : "transparent"
    border.width: 1
    border.color: primary ? theme.accent : danger ? theme.danger : theme.muted

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        Text {
            visible: root.glyph !== ""
            text: root.glyph
            textFormat: Text.PlainText
            font.family: "monospace"
            font.pixelSize: 14
            color: root.primary ? theme.accentForeground : root.tone
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: root.text !== ""
            text: root.text
            textFormat: Text.PlainText
            font.family: "monospace"
            font.pixelSize: root.compact ? 12 : 13
            color: root.primary ? theme.accentForeground : root.tone
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.enabledState ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: if (root.enabledState) root.clicked()
    }
}
