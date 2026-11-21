import QtQuick
import QtQuick.Layouts

// A section title with an optional line of explanation under it.
ColumnLayout {
    id: root
    property string text: ""
    property string hint: ""
    spacing: 2
    Layout.fillWidth: true
    Layout.topMargin: 18

    Text {
        text: root.text.toUpperCase()
        textFormat: Text.PlainText
        font.family: "monospace"
        font.pixelSize: 11
        font.letterSpacing: 1.5
        font.bold: true
        color: theme.accent
    }
    Text {
        visible: root.hint !== ""
        Layout.fillWidth: true
        text: root.hint
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        font.family: "monospace"
        font.pixelSize: 12
        color: theme.dim
    }
}
