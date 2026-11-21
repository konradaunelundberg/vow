import QtQuick
import QtQuick.Layouts

// One schedule rule. It edits the rule object in place and says so through
// `changed`, so the fields keep focus while the preview follows.
Rectangle {
    id: root

    property var rule: ({})
    property bool locked: false
    property int revision: 0
    signal changed()
    signal removed()

    readonly property string kind: rule.kind || "weekly"
    readonly property var dayNames: { translator.language; return [1, 2, 3, 4, 5, 6, 7].map(function(d) {
        return Qt.locale().dayName(d % 7, Locale.ShortFormat) }) }

    function set(key, value) { rule[key] = value; revision++; changed() }
    function has(key, value) { return (rule[key] || []).indexOf(value) >= 0 }
    function toggle(key, value) {
        var list = (rule[key] || []).slice()
        var i = list.indexOf(value)
        if (i >= 0) list.splice(i, 1); else list.push(value)
        list.sort(function(a, b) { return a - b })
        set(key, list)
    }
    function numbers(text) {
        return text.split(/[ ,]+/).map(function(s) { return parseInt(s) })
            .filter(function(n) { return !isNaN(n) && n !== 0 && n >= -31 && n <= 31 })
    }

    implicitHeight: body.implicitHeight + 24
    radius: Math.min(6, theme.radius)
    color: theme.surface
    border.width: 1
    border.color: theme.muted
    opacity: rule.enabled === false ? 0.6 : 1

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Txt {
                text: ({ weekly: qsTr("Weekly"), daily: qsTr("Daily"), monthly: qsTr("Monthly"), once: qsTr("One-off") })[root.kind]
                font.bold: true
                color: theme.accent
            }
            Txt {
                Layout.fillWidth: true
                text: { root.revision; translator.language; return backend.describeRule(root.rule) }
                dimmed: true
                elide: Text.ElideRight
                font.pixelSize: 12
            }
            Txt { visible: root.locked; text: "󰌾"; color: theme.warning }
            VButton {
                visible: !root.locked
                compact: true
                text: root.rule.enabled === false ? qsTr("Paused") : qsTr("On")
                onClicked: root.set("enabled", root.rule.enabled === false)
            }
            VButton { visible: !root.locked; compact: true; glyph: "󰅖"; danger: true; onClicked: root.removed() }
        }

        // ------------------------------------------------ which days
        Flow {
            visible: root.kind === "weekly"
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: 7
                Chip {
                    minWidth: 44
                    text: root.dayNames[index]
                    checked: { root.revision; return root.has("days", index + 1) }
                    locked: root.locked
                    onToggled: root.toggle("days", index + 1)
                }
            }
            Item { width: 8; height: 1 }
            Chip { text: qsTr("Weekdays"); locked: root.locked; onToggled: root.set("days", [1, 2, 3, 4, 5]) }
            Chip { text: qsTr("Weekend"); locked: root.locked; onToggled: root.set("days", [6, 7]) }
            Chip { text: qsTr("All"); locked: root.locked; onToggled: root.set("days", [1, 2, 3, 4, 5, 6, 7]) }
        }

        ColumnLayout {
            visible: root.kind === "monthly"
            Layout.fillWidth: true
            spacing: 6
            Flow {
                Layout.fillWidth: true
                spacing: 4
                Txt { text: qsTr("On the"); dimmed: true; width: 70; height: 26; verticalAlignment: Text.AlignVCenter }
                Repeater {
                    model: [[1, qsTr("1st")], [2, qsTr("2nd")], [3, qsTr("3rd")], [4, qsTr("4th")], [5, qsTr("5th")], [-1, qsTr("Last")]]
                    Chip {
                        minWidth: 44
                        text: modelData[1]
                        checked: { root.revision; return root.has("ordinals", modelData[0]) }
                        locked: root.locked
                        onToggled: root.toggle("ordinals", modelData[0])
                    }
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                Item { width: 70; height: 26 }
                Repeater {
                    model: 7
                    Chip {
                        minWidth: 44
                        text: root.dayNames[index]
                        checked: { root.revision; return root.has("weekdays", index + 1) }
                        locked: root.locked
                        onToggled: root.toggle("weekdays", index + 1)
                    }
                }
            }
            RowLayout {
                spacing: 8
                Txt { text: qsTr("or days"); dimmed: true; Layout.preferredWidth: 70 }
                VField {
                    fieldWidth: 160
                    readOnly: root.locked
                    text: (root.rule.monthDays || []).join(", ")
                    placeholder: "1, 15, -1"
                    onEdited: root.set("monthDays", root.numbers(text))
                }
                Txt { text: qsTr("negative counts from the end: -1 is the last day"); dimmed: true; font.pixelSize: 11 }
            }
        }

        RowLayout {
            visible: root.kind === "weekly" || root.kind === "daily"
            spacing: 8
            Txt { text: qsTr("Every"); dimmed: true }
            VField {
                fieldWidth: 52
                readOnly: root.locked
                text: String(root.rule.every || 1)
                valid: parseInt(text) >= 1
                input.validator: IntValidator { bottom: 1; top: 365 }
                onEdited: if (parseInt(text) >= 1) root.set("every", parseInt(text))
            }
            Txt { text: root.kind === "weekly" ? qsTr("week(s), counting from the week of") : qsTr("day(s), counting from"); dimmed: true }
            VField {
                fieldWidth: 120
                readOnly: root.locked
                text: root.rule.anchor || ""
                placeholder: "YYYY-MM-DD"
                valid: text === "" || backend.validDate(text)
                onEdited: if (text === "" || backend.validDate(text)) root.set("anchor", text)
            }
        }

        // ------------------------------------------------ when in the day
        RowLayout {
            visible: root.kind !== "once"
            spacing: 8
            Txt { text: qsTr("From"); dimmed: true }
            VField {
                id: startField
                fieldWidth: 70
                readOnly: root.locked
                text: root.rule.start || "00:00"
                valid: backend.validTime(text)
                onEdited: if (backend.validTime(text)) root.set("start", text)
            }
            Txt { text: qsTr("to"); dimmed: true }
            VField {
                id: endField
                fieldWidth: 70
                readOnly: root.locked
                text: root.rule.end || "24:00"
                valid: backend.validTime(text)
                onEdited: if (backend.validTime(text)) root.set("end", text)
            }
            Chip {
                text: qsTr("All day")
                locked: root.locked
                checked: { root.revision; return (root.rule.start || "00:00") === "00:00" && ["24:00", "00:00"].indexOf(root.rule.end || "24:00") >= 0 }
                onToggled: { startField.text = "00:00"; endField.text = "24:00"; root.rule.start = "00:00"; root.set("end", "24:00") }
            }
            Txt {
                visible: { root.revision; var s = root.rule.start || "00:00", e = root.rule.end || "24:00"; return e !== "24:00" && e <= s && !(s === "00:00" && e === "00:00") }
                text: qsTr("ends the next day")
                color: theme.warning
                font.pixelSize: 12
            }
        }

        RowLayout {
            visible: root.kind === "once"
            spacing: 8
            Txt { text: qsTr("From"); dimmed: true }
            VField {
                fieldWidth: 170
                readOnly: root.locked
                text: (root.rule.startAt || "").replace("T", " ").slice(0, 16)
                placeholder: "YYYY-MM-DD HH:MM"
                valid: backend.validDateTime(text)
                onEdited: if (backend.validDateTime(text)) root.set("startAt", backend.normalizeDateTime(text))
            }
            Txt { text: qsTr("until"); dimmed: true }
            VField {
                fieldWidth: 170
                readOnly: root.locked
                text: (root.rule.endAt || "").replace("T", " ").slice(0, 16)
                placeholder: "YYYY-MM-DD HH:MM"
                valid: backend.validDateTime(text)
                onEdited: if (backend.validDateTime(text)) root.set("endAt", backend.normalizeDateTime(text))
            }
        }

        // ------------------------------------------------ narrowing
        Txt {
            visible: root.kind !== "once"
            text: (more.shown ? "▾ " : "▸ ") + qsTr("Months and date range")
            dimmed: true
            font.pixelSize: 12
            MouseArea { id: more; property bool shown: false; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: shown = !shown }
        }
        Flow {
            visible: more.shown && root.kind !== "once"
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: 12
                Chip {
                    minWidth: 44
                    text: { translator.language; return Qt.locale().monthName(index, Locale.ShortFormat) }
                    checked: { root.revision; return root.has("months", index + 1) }
                    locked: root.locked
                    onToggled: root.toggle("months", index + 1)
                }
            }
        }
        RowLayout {
            visible: more.shown && root.kind !== "once"
            spacing: 8
            Txt { text: qsTr("Only from"); dimmed: true }
            VField {
                fieldWidth: 120
                readOnly: root.locked
                text: root.rule.validFrom || ""
                placeholder: "YYYY-MM-DD"
                valid: text === "" || backend.validDate(text)
                onEdited: if (text === "" || backend.validDate(text)) root.set("validFrom", text)
            }
            Txt { text: qsTr("until"); dimmed: true }
            VField {
                fieldWidth: 120
                readOnly: root.locked
                text: root.rule.validUntil || ""
                placeholder: "YYYY-MM-DD"
                valid: text === "" || backend.validDate(text)
                onEdited: if (text === "" || backend.validDate(text)) root.set("validUntil", text)
            }
            Txt { text: qsTr("no months picked means every month"); dimmed: true; font.pixelSize: 11 }
        }
    }
}
