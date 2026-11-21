import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Edits a copy of one block and hands it to the daemon on save. While a
// block is locked the daemon only takes changes that make it stricter, and
// the editor says so up front rather than letting the save bounce.
Item {
    id: root

    property var draft: null
    property bool dirty: false
    property int revision: 0
    property int structure: 0
    property string error: ""
    readonly property bool isNew: draft !== null && !draft.id
    readonly property var evaluation: draft && draft.id ? (backend.evaluations[draft.id] || {}) : ({})
    readonly property bool locked: evaluation.locked === true
    property int lockedRules: 0

    signal saved(string id)
    signal deleted()
    signal toast(string text)

    function clone(value) { return JSON.parse(JSON.stringify(value)) }

    function load(block) {
        var b = clone(block)
        var keys = ["sites", "apps", "allowSites", "rules", "exceptions", "triggers", "conditions"]
        for (var i = 0; i < keys.length; i++) if (!Array.isArray(b[keys[i]])) b[keys[i]] = []
        if (!b.limit) b.limit = { minutes: 0, period: "day" }
        if (!b.allowances) b.allowances = {}
        if (!b.allowances.breaks) b.allowances.breaks = { minutes: 0, per: 60 }
        if (!b.allowances.passes) b.allowances.passes = { count: 0, minutes: 5 }
        draft = b
        lockedRules = b.rules.length
        dirty = false
        error = ""
        revision++
        structure++
    }

    function touch() { dirty = true; revision++ }
    function rebuild() { dirty = true; revision++; structure++ }

    function trigger(type) {
        for (var i = 0; i < draft.triggers.length; i++) if (draft.triggers[i].type === type) return draft.triggers[i]
        return null
    }
    function condition(type) {
        for (var i = 0; i < draft.conditions.length; i++) if (draft.conditions[i].type === type) return draft.conditions[i]
        return null
    }

    function save() {
        if (!draft) return
        if (!draft.name || draft.name.trim() === "") { error = qsTr("Give the block a name first."); return }
        var result = backend.save(draft)
        if (result !== "") { error = result; return }
        error = ""
        dirty = false
        var match = backend.blocks.filter(function(b) { return b.name === draft.name })
        var id = draft.id || (match.length ? match[match.length - 1].id : "")
        root.toast(qsTr("Saved %1").arg(draft.name))
        root.saved(id)
    }

    // ------------------------------------------------------------------ view
    Txt {
        anchors.centerIn: parent
        visible: root.draft === null
        text: qsTr("Pick a block, or press N for a new one.")
        dimmed: true
    }

    Flickable {
        id: flick
        visible: root.draft !== null
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + 60
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        ColumnLayout {
            id: column
            x: 28
            y: 20
            width: Math.min(820, flick.width - 56)
            spacing: 8

            // ---------------------------------------------- header
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                VField {
                    id: nameField
                    Layout.fillWidth: true
                    pixelSize: 18
                    placeholder: qsTr("Block name, e.g. Deep work")
                    text: root.draft ? root.draft.name : ""
                    onEdited: { root.draft.name = text; root.touch() }
                    onAccepted: root.save()
                }
                VButton {
                    visible: !root.locked
                    text: root.draft && root.draft.enabled === false ? qsTr("Disabled") : qsTr("Enabled")
                    onClicked: { root.draft.enabled = root.draft.enabled === false; root.touch() }
                }
                VButton { text: qsTr("Save"); glyph: "󰆓"; primary: true; enabledState: root.dirty || root.isNew; onClicked: root.save() }
                VButton {
                    visible: !root.isNew && !root.locked
                    glyph: "󰆴"
                    danger: true
                    onClicked: confirmDelete.open()
                }
            }

            Rectangle {
                visible: root.locked
                Layout.fillWidth: true
                implicitHeight: lockText.implicitHeight + 20
                color: Qt.rgba(theme.warning.r, theme.warning.g, theme.warning.b, 0.12)
                border.color: theme.warning
                radius: Math.min(4, theme.radius)
                Txt {
                    id: lockText
                    anchors.fill: parent
                    anchors.margins: 10
                    wrapMode: Text.WordWrap
                    color: theme.warning
                    text: "󰌾  " + qsTr("Locked until %1 (%2 left). You can add sites, apps, and rules, or extend the lock; nothing that loosens it.")
                        .arg(backend.clock(root.evaluation.lockUntil)).arg(backend.remaining(root.evaluation.lockUntil))
                }
            }

            Txt {
                visible: root.error !== ""
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: root.error
                color: theme.danger
            }

            // ---------------------------------------------- what
            Heading { text: qsTr("What"); hint: qsTr("Block a list, or allow only a list and block everything else.") }
            RowLayout {
                spacing: 4
                Chip {
                    text: qsTr("Block these")
                    locked: root.locked
                    checked: { root.revision; return root.draft && root.draft.mode !== "allow" }
                    onToggled: { root.draft.mode = "block"; root.touch() }
                }
                Chip {
                    text: qsTr("Allow only these")
                    locked: root.locked
                    checked: { root.revision; return root.draft && root.draft.mode === "allow" }
                    onToggled: { root.draft.mode = "allow"; root.touch() }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: width > 640 ? 2 : 1
                columnSpacing: 14
                rowSpacing: 6

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 4
                    Txt { text: "󰖟  " + qsTr("Websites"); font.bold: true }
                    Txt { text: qsTr("one per line · reddit.com covers subdomains · youtube.com/shorts blocks a path · * is everything"); dimmed: true; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    VArea {
                        Layout.fillWidth: true
                        lines: 6
                        placeholder: "reddit.com\nx.com\nyoutube.com/shorts"
                        text: { root.structure; return root.draft ? backend.listToText(root.draft.sites) : "" }
                        onEdited: { root.draft.sites = backend.textToList(text); root.touch() }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 4
                    Txt { text: "󰀻  " + qsTr("Apps"); font.bold: true }
                    Txt { text: qsTr("process name (steam) or window class (class:discord)"); dimmed: true; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    VArea {
                        Layout.fillWidth: true
                        lines: 6
                        placeholder: "steam\nclass:discord"
                        text: { root.structure; return root.draft ? backend.listToText(root.draft.apps) : "" }
                        onEdited: { root.draft.apps = backend.textToList(text); root.touch() }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Txt { text: root.draft && root.draft.mode === "allow" ? qsTr("Also allowed") : qsTr("Always allowed (exceptions)"); font.bold: true }
                Txt { text: qsTr("let these through even while this block is on, e.g. youtube.com/watch?v=lecture"); dimmed: true; font.pixelSize: 11 }
                VArea {
                    Layout.fillWidth: true
                    lines: 2
                    text: { root.structure; return root.draft ? backend.listToText(root.draft.allowSites) : "" }
                    onEdited: { root.draft.allowSites = backend.textToList(text); root.touch() }
                }
            }

            // ---------------------------------------------- when
            Heading {
                text: qsTr("When")
                hint: qsTr("Rules add up; the block is on whenever any of them is. Leave this empty for a block you only start by hand, or for a limit that applies all day.")
            }
            Repeater {
                model: { root.structure; return root.draft ? root.draft.rules.length : 0 }
                RuleEditor {
                    Layout.fillWidth: true
                    rule: root.draft.rules[index]
                    locked: root.locked && index < root.lockedRules
                    onChanged: root.touch()
                    onRemoved: { root.draft.rules.splice(index, 1); root.rebuild() }
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: [["weekly", qsTr("Weekly")], ["daily", qsTr("Daily")], ["monthly", qsTr("Monthly")], ["once", qsTr("One-off")]]
                    VButton {
                        compact: true
                        glyph: "󰐕"
                        text: modelData[1]
                        onClicked: { root.draft.rules.push(backend.newRule(modelData[0])); root.rebuild() }
                    }
                }
            }
            WeekPreview {
                Layout.fillWidth: true
                Layout.topMargin: 8
                visible: root.draft && root.draft.rules.length > 0
                block: root.draft
                revision: root.revision
                days: 14
            }

            // ---------------------------------------------- exceptions
            Heading { text: qsTr("Days off"); hint: qsTr("Holidays and other days the schedule skips. A manual session still works on them.") }
            Repeater {
                model: { root.structure; return root.draft ? root.draft.exceptions.length : 0 }
                RowLayout {
                    spacing: 8
                    readonly property var exception: root.draft.exceptions[index]
                    Txt { text: qsTr("From"); dimmed: true }
                    VField {
                        fieldWidth: 120
                        readOnly: root.locked
                        text: exception.from || ""
                        placeholder: "YYYY-MM-DD"
                        valid: backend.validDate(text)
                        onEdited: if (backend.validDate(text)) { exception.from = text; root.touch() }
                    }
                    Txt { text: qsTr("to"); dimmed: true }
                    VField {
                        fieldWidth: 120
                        readOnly: root.locked
                        text: exception.to || ""
                        placeholder: qsTr("same day")
                        valid: text === "" || backend.validDate(text)
                        onEdited: if (text === "" || backend.validDate(text)) { exception.to = text; root.touch() }
                    }
                    VField {
                        fieldWidth: 200
                        text: exception.label || ""
                        placeholder: qsTr("label (optional)")
                        onEdited: { exception.label = text; root.touch() }
                    }
                    VButton { visible: !root.locked; compact: true; glyph: "󰅖"; danger: true; onClicked: { root.draft.exceptions.splice(index, 1); root.rebuild() } }
                }
            }
            VButton {
                visible: !root.locked
                compact: true
                glyph: "󰐕"
                text: qsTr("Day off")
                onClicked: {
                    var today = new Date().toISOString().slice(0, 10)
                    root.draft.exceptions.push({ from: today, to: "", label: "" })
                    root.rebuild()
                }
            }

            // ---------------------------------------------- triggers
            Heading { text: qsTr("Triggers and conditions"); hint: qsTr("A trigger turns the block on by itself; a condition has to hold for it to bite at all.") }
            RowLayout {
                spacing: 8
                Chip {
                    text: qsTr("On login")
                    locked: root.locked
                    checked: { root.revision; return root.draft && root.trigger("login") !== null }
                    onToggled: {
                        var t = root.trigger("login")
                        if (t) root.draft.triggers.splice(root.draft.triggers.indexOf(t), 1)
                        else root.draft.triggers.push({ type: "login", minutes: 30 })
                        root.rebuild()
                    }
                }
                Txt { visible: { root.structure; return root.draft && root.trigger("login") !== null }
                    text: qsTr("block for the first"); dimmed: true }
                VField {
                    visible: { root.structure; return root.draft && root.trigger("login") !== null }
                    fieldWidth: 60
                    readOnly: root.locked
                    text: { root.structure; var t = root.draft && root.trigger("login"); return t ? String(t.minutes) : "30" }
                    input.validator: IntValidator { bottom: 1; top: 1440 }
                    onEdited: { var t = root.trigger("login"); if (t && parseInt(text) > 0) { t.minutes = parseInt(text); root.touch() } }
                }
                Txt { visible: { root.structure; return root.draft && root.trigger("login") !== null }
                    text: qsTr("minutes of each session"); dimmed: true }
            }
            RowLayout {
                spacing: 8
                Chip {
                    text: qsTr("Wi-Fi")
                    locked: root.locked
                    checked: { root.revision; return root.draft && root.condition("wifi") !== null }
                    onToggled: {
                        var c = root.condition("wifi")
                        if (c) root.draft.conditions.splice(root.draft.conditions.indexOf(c), 1)
                        else root.draft.conditions.push({ type: "wifi", match: "on", networks: backend.ssid ? [backend.ssid] : [] })
                        root.rebuild()
                    }
                }
                Repeater {
                    model: { root.structure; return root.draft && root.condition("wifi") ? [["on", qsTr("only on")], ["off", qsTr("except on")]] : [] }
                    Chip {
                        text: modelData[1]
                        locked: root.locked
                        checked: { root.revision; var c = root.condition("wifi"); return c && c.match === modelData[0] }
                        onToggled: { root.condition("wifi").match = modelData[0]; root.touch() }
                    }
                }
                VField {
                    visible: { root.structure; return root.draft && root.condition("wifi") !== null }
                    fieldWidth: 260
                    readOnly: root.locked
                    placeholder: qsTr("network names, comma separated")
                    text: { root.structure; var c = root.draft && root.condition("wifi"); return c ? c.networks.join(", ") : "" }
                    onEdited: { root.condition("wifi").networks = backend.textToList(text); root.touch() }
                }
                Txt {
                    visible: { root.structure; return root.draft && root.condition("wifi") !== null && backend.ssid !== "" }
                    text: qsTr("now on %1").arg(backend.ssid)
                    dimmed: true
                    font.pixelSize: 11
                }
            }

            // ---------------------------------------------- limit
            Heading { text: qsTr("Time limit"); hint: qsTr("Instead of blocking outright, allow this much focused time, then block until the period ends. Rules above set when the limit counts.") }
            RowLayout {
                spacing: 8
                Chip {
                    text: qsTr("Limit")
                    locked: root.locked
                    checked: { root.revision; return root.draft && root.draft.limit.minutes > 0 }
                    onToggled: { root.draft.limit.minutes = root.draft.limit.minutes > 0 ? 0 : 45; root.rebuild() }
                }
                VField {
                    visible: { root.structure; return root.draft && root.draft.limit.minutes > 0 }
                    fieldWidth: 70
                    readOnly: root.locked
                    text: { root.structure; return root.draft ? String(root.draft.limit.minutes) : "" }
                    input.validator: IntValidator { bottom: 1; top: 10080 }
                    onEdited: if (parseInt(text) > 0) { root.draft.limit.minutes = parseInt(text); root.touch() }
                }
                Txt { visible: { root.structure; return root.draft && root.draft.limit.minutes > 0 }
                    text: qsTr("minutes per"); dimmed: true }
                Repeater {
                    model: { root.structure; return root.draft && root.draft.limit.minutes > 0 ? [["hour", qsTr("hour")], ["day", qsTr("day")], ["week", qsTr("week")]] : [] }
                    Chip {
                        text: modelData[1]
                        locked: root.locked
                        checked: { root.revision; return root.draft.limit.period === modelData[0] }
                        onToggled: { root.draft.limit.period = modelData[0]; root.touch() }
                    }
                }
            }

            // ---------------------------------------------- allowances
            Heading { text: qsTr("Allowances"); hint: qsTr("Planned slack, written into the block. They work while it is locked; nothing else does.") }
            RowLayout {
                spacing: 8
                Txt { text: qsTr("Breaks"); Layout.preferredWidth: 70 }
                VField {
                    fieldWidth: 60
                    readOnly: root.locked
                    text: { root.structure; return root.draft ? String(root.draft.allowances.breaks.minutes || 0) : "0" }
                    input.validator: IntValidator { bottom: 0; top: 1440 }
                    onEdited: { root.draft.allowances.breaks.minutes = parseInt(text) || 0; root.touch() }
                }
                Txt { text: qsTr("minutes of break every"); dimmed: true }
                VField {
                    fieldWidth: 60
                    readOnly: root.locked
                    text: { root.structure; return root.draft ? String(root.draft.allowances.breaks.per || 60) : "60" }
                    input.validator: IntValidator { bottom: 1; top: 1440 }
                    onEdited: if (parseInt(text) > 0) { root.draft.allowances.breaks.per = parseInt(text); root.touch() }
                }
                Txt { text: qsTr("minutes"); dimmed: true }
            }
            RowLayout {
                spacing: 8
                Txt { text: qsTr("Passes"); Layout.preferredWidth: 70 }
                VField {
                    fieldWidth: 60
                    readOnly: root.locked
                    text: { root.structure; return root.draft ? String(root.draft.allowances.passes.count || 0) : "0" }
                    input.validator: IntValidator { bottom: 0; top: 100 }
                    onEdited: { root.draft.allowances.passes.count = parseInt(text) || 0; root.touch() }
                }
                Txt { text: qsTr("per day, each lasting"); dimmed: true }
                VField {
                    fieldWidth: 60
                    readOnly: root.locked
                    text: { root.structure; return root.draft ? String(root.draft.allowances.passes.minutes || 5) : "5" }
                    input.validator: IntValidator { bottom: 1; top: 240 }
                    onEdited: if (parseInt(text) > 0) { root.draft.allowances.passes.minutes = parseInt(text); root.touch() }
                }
                Txt { text: qsTr("minutes"); dimmed: true }
            }

            // ---------------------------------------------- lock
            Heading {
                visible: !root.isNew
                text: qsTr("Lock")
                hint: qsTr("A locked block cannot be deleted, disabled, or loosened until the timer runs out. There is no way to unlock it early.")
            }
            RowLayout {
                visible: !root.isNew
                spacing: 8
                Txt { text: root.locked ? qsTr("Extend to") : qsTr("Lock for"); dimmed: true }
                Repeater {
                    model: [["1d", qsTr("a day")], ["7d", qsTr("a week")], ["30d", qsTr("30 days")]]
                    Chip { text: modelData[1]; onToggled: lockField.text = modelData[0] }
                }
                VField {
                    id: lockField
                    fieldWidth: 190
                    placeholder: qsTr("8h, 3d, or YYYY-MM-DD HH:MM")
                    valid: text === "" || backend.normalizeDateTime(text) !== ""
                }
                VButton {
                    text: qsTr("Lock…")
                    glyph: "󰌾"
                    enabledState: lockField.text !== "" && backend.normalizeDateTime(lockField.text) !== "" && !root.dirty
                    onClicked: confirmLock.open()
                }
                Txt { visible: root.dirty; text: qsTr("save first"); color: theme.warning; font.pixelSize: 11 }
            }
        }
    }

    ConfirmDialog {
        id: confirmLock
        title: qsTr("Lock %1?").arg(root.draft ? root.draft.name : "")
        message: qsTr("Until %1. You will not be able to delete, disable, or loosen this block before then, and there is no early unlock.")
            .arg(backend.clock(backend.normalizeDateTime(lockField.text)))
        confirmText: qsTr("Lock it")
        onConfirmed: {
            var result = backend.lock(root.draft.id, lockField.text)
            if (result !== "") root.error = result
            else { lockField.text = ""; root.toast(qsTr("Locked")) }
        }
    }

    ConfirmDialog {
        id: confirmDelete
        title: qsTr("Delete %1?").arg(root.draft ? root.draft.name : "")
        message: qsTr("Its schedule and settings go with it. Usage statistics stay.")
        confirmText: qsTr("Delete")
        danger: true
        onConfirmed: {
            var result = backend.remove(root.draft.id)
            if (result !== "") root.error = result
            else { root.draft = null; root.deleted() }
        }
    }
}
