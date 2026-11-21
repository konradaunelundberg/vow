import QtQuick

// A single-line field. It reports edits as they happen through `edited`,
// and outlines itself in red when `valid` is false.
Rectangle {
    id: root

    property alias text: input.text
    property string placeholder: ""
    property bool valid: true
    property bool readOnly: false
    property int fieldWidth: 120
    property alias input: input
    property int pixelSize: 13
    signal edited(string text)
    signal accepted()

    implicitWidth: fieldWidth
    implicitHeight: Math.max(30, input.implicitHeight + 12)
    radius: Math.min(4, theme.radius)
    color: theme.surface
    border.width: 1
    border.color: !valid ? theme.danger : input.activeFocus ? theme.accent : theme.muted
    opacity: readOnly ? 0.6 : 1

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        verticalAlignment: TextInput.AlignVCenter
        font.family: "monospace"
        font.pixelSize: root.pixelSize
        color: theme.foreground
        selectionColor: theme.accent
        selectedTextColor: theme.accentForeground
        readOnly: root.readOnly
        clip: true
        selectByMouse: true
        onTextEdited: root.edited(text)
        onAccepted: root.accepted()
        Keys.onEscapePressed: focus = false

        Text {
            anchors.fill: parent
            verticalAlignment: Text.AlignVCenter
            visible: input.text === "" && !input.activeFocus
            text: root.placeholder
            textFormat: Text.PlainText
            font: input.font
            color: theme.dim
            elide: Text.ElideRight
        }
    }
}
