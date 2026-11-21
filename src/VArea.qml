import QtQuick

// A multi-line field for lists, one entry per line.
Rectangle {
    id: root

    property alias text: edit.text
    property string placeholder: ""
    property bool readOnly: false
    property int lines: 4
    signal edited(string text)

    implicitHeight: Math.max(lines * 18 + 14, edit.contentHeight + 14)
    radius: Math.min(4, theme.radius)
    color: theme.surface
    border.width: 1
    border.color: edit.activeFocus ? theme.accent : theme.muted

    TextEdit {
        id: edit
        anchors.fill: parent
        anchors.margins: 7
        anchors.leftMargin: 8
        font.family: "monospace"
        font.pixelSize: 13
        color: theme.foreground
        selectionColor: theme.accent
        selectedTextColor: theme.accentForeground
        wrapMode: TextEdit.NoWrap
        selectByMouse: true
        readOnly: root.readOnly
        textFormat: TextEdit.PlainText
        onTextChanged: if (activeFocus) root.edited(text)
        Keys.onEscapePressed: focus = false

        Text {
            visible: edit.text === "" && !edit.activeFocus
            text: root.placeholder
            textFormat: Text.PlainText
            font: edit.font
            color: theme.dim
            lineHeight: 1
        }
    }
}
