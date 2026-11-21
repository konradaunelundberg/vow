import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root

    property string title: ""
    property string message: ""
    property string confirmText: qsTr("OK")
    property bool danger: false
    signal confirmed()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(460, parent ? parent.width - 40 : 460)
    modal: true
    focus: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: theme.surface
        border.color: root.danger ? theme.danger : theme.accent
        border.width: 1
        radius: Math.min(8, theme.radius)
    }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }

    contentItem: ColumnLayout {
        spacing: 12
        Txt { text: root.title; font.pixelSize: 16; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Txt { text: root.message; dimmed: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            VButton { text: qsTr("Cancel"); onClicked: root.close() }
            VButton {
                text: root.confirmText
                primary: !root.danger
                danger: root.danger
                onClicked: { root.close(); root.confirmed() }
            }
        }
        Keys.onReturnPressed: { root.close(); root.confirmed() }
    }
}
