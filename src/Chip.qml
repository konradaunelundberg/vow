import QtQuick

// A toggle for one choice among few: days of the week, durations, modes.
Rectangle {
    id: root

    property string text: ""
    property bool checked: false
    property bool locked: false
    property int minWidth: 0
    signal toggled()

    implicitWidth: Math.max(minWidth, label.implicitWidth + 18)
    implicitHeight: 26
    radius: Math.min(4, theme.radius)
    color: checked ? theme.accent : mouse.containsMouse && !locked ? theme.surfaceHigh : "transparent"
    border.width: 1
    border.color: checked ? theme.accent : theme.muted
    opacity: locked && !checked ? 0.5 : 1

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        textFormat: Text.PlainText
        font.family: "monospace"
        font.pixelSize: 12
        color: root.checked ? theme.accentForeground : theme.foreground
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.locked ? Qt.ArrowCursor : Qt.PointingHandCursor
        onClicked: if (!root.locked) root.toggled()
    }
}
