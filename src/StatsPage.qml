import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Where the focused time went: apps by window class, sites by host.
Item {
    id: root

    property int days: 1
    property int tick: 0
    readonly property var items: { tick; backend.stats; return backend.usage(days).slice(0, 40) }
    readonly property var totals: { tick; backend.stats; return backend.dailyTotals(14) }
    readonly property real peak: { var m = 1; for (var i = 0; i < totals.length; i++) m = Math.max(m, totals[i].apps); return m }
    readonly property real topSeconds: items.length ? items[0].seconds : 1

    // The daemon's counters move every second; the page redraws every ten.
    Timer { interval: 10000; running: root.visible; repeat: true; onTriggered: root.tick++ }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + 60
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        ColumnLayout {
            id: column
            x: 32
            y: 28
            width: Math.min(860, flick.width - 64)
            spacing: 10

            Txt { text: qsTr("Usage"); font.pixelSize: 28; font.bold: true }
            Txt { text: qsTr("Focused time per window and site, counted by the Vow service."); dimmed: true }

            Heading { text: qsTr("Last 14 days") }
            Row {
                Layout.fillWidth: true
                height: 140
                spacing: 6
                Repeater {
                    model: root.totals
                    Column {
                        width: (column.width - 13 * 6) / 14
                        height: 140
                        spacing: 4
                        Item {
                            width: parent.width
                            height: 110
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: Math.max(1, modelData.apps / root.peak * parent.height)
                                color: index === root.totals.length - 1 ? theme.accent : theme.surfaceHigh
                                radius: Math.min(2, theme.radius)
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: modelData.apps > 0 ? Math.min(parent.height, modelData.sites / modelData.apps * parent.height) : 0
                                    color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.18)
                                    radius: parent.radius
                                }
                            }
                            Txt {
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: Math.max(1, modelData.apps / root.peak * parent.height) + 2
                                anchors.horizontalCenter: parent.horizontalCenter
                                visible: modelData.apps >= 60
                                text: Math.round(modelData.apps / 3600 * 10) / 10 + "h"
                                font.pixelSize: 10
                                dimmed: true
                            }
                        }
                        Txt { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.label; font.pixelSize: 10; dimmed: true }
                    }
                }
            }
            Txt { text: qsTr("Bar: all focused time · lighter part: of which in the browser on a known site"); dimmed: true; font.pixelSize: 11 }

            RowLayout {
                Layout.topMargin: 18
                spacing: 4
                Repeater {
                    model: [[1, qsTr("Today")], [7, qsTr("7 days")], [14, qsTr("14 days")]]
                    Chip { text: modelData[1]; checked: root.days === modelData[0]; onToggled: root.days = modelData[0] }
                }
            }

            Txt { visible: root.items.length === 0; text: qsTr("Nothing counted yet."); dimmed: true; Layout.topMargin: 10 }

            Repeater {
                model: root.items
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Txt { text: modelData.kind === "site" ? "󰖟" : "󰀻"; color: modelData.kind === "site" ? theme.accent : theme.dim }
                    Txt { text: modelData.name; Layout.preferredWidth: 240; elide: Text.ElideRight }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 6
                        radius: Math.min(3, theme.radius)
                        color: theme.surface
                        Rectangle {
                            width: parent.width * modelData.seconds / root.topSeconds
                            height: parent.height
                            radius: parent.radius
                            color: modelData.kind === "site" ? theme.accent : theme.foreground
                            opacity: modelData.kind === "site" ? 0.9 : 0.5
                        }
                    }
                    Txt { text: backend.duration(modelData.seconds); Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight; dimmed: true }
                }
            }
        }
    }
}
