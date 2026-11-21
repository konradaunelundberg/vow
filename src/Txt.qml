import QtQuick

Text {
    property bool dimmed: false
    textFormat: Text.PlainText
    font.family: "monospace"
    font.pixelSize: 13
    color: dimmed ? theme.dim : theme.foreground
}
