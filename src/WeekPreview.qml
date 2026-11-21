import QtQuick
import QtQuick.Layouts

// The coming days as rows of 24 hours, with the block's scheduled time
// filled in: what the rules add up to, exceptions and all.
ColumnLayout {
    id: root

    property var block: null
    property int revision: 0
    property int days: 7
    readonly property var pieces: { revision; return block ? backend.preview(block, days) : [] }
    spacing: 3

    Row {
        Layout.fillWidth: true
        Layout.leftMargin: 76
        Repeater {
            model: [0, 3, 6, 9, 12, 15, 18, 21]
            Txt {
                width: (root.width - 76) / 8
                text: String(modelData).padStart(2, "0")
                font.pixelSize: 10
                dimmed: true
            }
        }
    }

    Repeater {
        model: root.days
        delegate: RowLayout {
            id: dayRow
            required property int index
            readonly property date date: {
                var d = new Date()
                d.setHours(0, 0, 0, 0)
                d.setDate(d.getDate() + index)
                return d
            }
            Layout.fillWidth: true
            spacing: 6

            Txt {
                Layout.preferredWidth: 70
                text: { translator.language; return dayRow.date.toLocaleDateString(Qt.locale(), "ddd d MMM") }
                font.pixelSize: 11
                color: dayRow.index === 0 ? theme.accent : theme.dim
            }

            Rectangle {
                id: track
                Layout.fillWidth: true
                height: 14
                color: theme.surface
                radius: Math.min(2, theme.radius)
                border.width: 1
                border.color: Qt.rgba(theme.muted.r, theme.muted.g, theme.muted.b, 0.6)

                Repeater {
                    model: 7
                    Rectangle {
                        x: (index + 1) * track.width / 8
                        width: 1
                        height: track.height
                        color: Qt.rgba(theme.muted.r, theme.muted.g, theme.muted.b, 0.5)
                    }
                }

                Repeater {
                    model: root.pieces.filter(function(p) { return p.day === dayRow.index })
                    Rectangle {
                        x: modelData.start / 1440 * track.width
                        width: Math.max(2, (modelData.end - modelData.start) / 1440 * track.width)
                        height: track.height
                        color: theme.accent
                        opacity: 0.85
                        radius: Math.min(2, theme.radius)
                    }
                }

                Rectangle {
                    visible: dayRow.index === 0
                    x: { backend.now; var n = new Date(); return (n.getHours() * 60 + n.getMinutes()) / 1440 * track.width }
                    width: 2
                    height: track.height + 4
                    y: -2
                    color: theme.danger
                }
            }
        }
    }
}
