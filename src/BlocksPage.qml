import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property string selected: ""
    readonly property bool dirty: editor.dirty
    signal toast(string text)

    function newBlock() {
        selected = ""
        editor.load(backend.newBlock())
        editor.forceActiveFocus()
    }
    function open(id) {
        var match = backend.blocks.filter(function(b) { return b.id === id })
        if (match.length) { selected = id; editor.load(match[0]) }
    }
    function save() { editor.save() }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            color: theme.surface

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Txt { text: qsTr("Blocks"); font.pixelSize: 16; font.bold: true; Layout.fillWidth: true }
                    VButton { compact: true; glyph: "󰐕"; text: qsTr("New"); onClicked: root.newBlock() }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.topMargin: 6
                    clip: true
                    spacing: 4
                    model: backend.blocks
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        readonly property var evaluation: backend.evaluations[modelData.id] || {}
                        width: ListView.view.width
                        height: 54
                        radius: Math.min(4, theme.radius)
                        color: root.selected === modelData.id ? theme.surfaceHigh : rowMouse.containsMouse ? Qt.rgba(theme.surfaceHigh.r, theme.surfaceHigh.g, theme.surfaceHigh.b, 0.5) : "transparent"
                        border.width: root.selected === modelData.id ? 1 : 0
                        border.color: theme.accent

                        Rectangle {
                            width: 3
                            height: parent.height - 16
                            anchors.verticalCenter: parent.verticalCenter
                            x: 4
                            radius: 1
                            color: row.evaluation.suspended ? theme.success : row.evaluation.blocking ? theme.accent : "transparent"
                        }

                        ColumnLayout {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 14
                            anchors.rightMargin: 10
                            spacing: 3
                            RowLayout {
                                Layout.fillWidth: true
                                Txt {
                                    text: row.modelData.name
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    font.bold: row.evaluation.blocking === true
                                    opacity: row.modelData.enabled === false ? 0.5 : 1
                                }
                                Txt { visible: row.evaluation.locked === true; text: "󰌾"; color: theme.warning }
                            }
                            Txt {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font.pixelSize: 11
                                dimmed: true
                                text: row.evaluation.blocking ? (row.evaluation.reason === "permanent" ? qsTr("blocking · until turned off") : qsTr("blocking · until %1").arg(backend.clock(row.evaluation.endsAt) || "—"))
                                    : row.modelData.enabled === false ? qsTr("disabled")
                                    : backend.describeBlock(row.modelData)
                            }
                        }
                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.open(row.modelData.id)
                        }
                    }
                }

                Txt {
                    visible: backend.blocks.length === 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    dimmed: true
                    text: qsTr("No blocks yet. A block is a list of sites and apps plus when to block them.")
                }
            }
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: theme.muted }

        BlockEditor {
            id: editor
            Layout.fillWidth: true
            Layout.fillHeight: true
            onSaved: function(id) { root.selected = id; root.open(id) }
            onDeleted: root.selected = ""
            onToast: function(text) { root.toast(text) }
        }
    }
}
