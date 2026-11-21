import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// What is blocked right now, what comes next, and a quick way to start a
// session: the page Vow opens on.
Item {
    id: root

    property string quickBlock: ""
    property int quickMinutes: 50
    property bool quickLock: false
    signal toast(string text)
    signal editBlock(string id)

    readonly property var active: backend.status.active || []
    readonly property var next: backend.status.next || ({})
    readonly property var limited: backend.blocks.filter(function(b) { return b.limit && b.limit.minutes > 0 })
    readonly property var upcoming: {
        var list = []
        for (var i = 0; i < backend.blocks.length; i++) {
            var b = backend.blocks[i]
            var e = backend.evaluations[b.id] || {}
            if (e.nextStart && !e.blocking && !(b.limit && b.limit.minutes > 0)) list.push({ id: b.id, name: b.name, at: e.nextStart })
        }
        list.sort(function(a, b) { return a.at < b.at ? -1 : 1 })
        return list.slice(0, 6)
    }

    function startQuick() {
        var id = quickBlock !== "" ? quickBlock : (backend.blocks.length ? backend.blocks[0].id : "")
        if (id === "") { toast(qsTr("Make a block first (press 2, then N).")); return }
        if (quickLock) { confirmQuickLock.open(); return }
        var result = backend.startSession(id, quickMinutes, false)
        toast(result === "" ? qsTr("Blocking for %1").arg(backend.inMinutes(quickMinutes)) : result)
    }

    function act(result, success) { toast(result === "" ? success : result) }

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
            spacing: 12

            // ---------------------------------------------- the hero
            Txt {
                visible: root.active.length === 0
                text: qsTr("Nothing is blocked.")
                font.pixelSize: 28
                font.bold: true
            }
            Txt {
                visible: root.active.length === 0
                text: root.next.name ? qsTr("Next: %1 at %2, in %3").arg(root.next.name).arg(backend.clock(root.next.startsAt))
                                           .arg((backend.now, backend.remaining(root.next.startsAt)))
                                     : qsTr("No scheduled blocks coming up.")
                dimmed: true
            }

            Repeater {
                model: root.active
                Rectangle {
                    id: card
                    required property var modelData
                    readonly property var evaluation: backend.evaluations[modelData.id] || {}
                    readonly property bool onBreak: modelData.suspended
                    Layout.fillWidth: true
                    implicitHeight: cardBody.implicitHeight + 32
                    color: theme.surface
                    radius: Math.min(8, theme.radius)
                    border.width: 1
                    border.color: onBreak ? theme.success : theme.accent

                    ColumnLayout {
                        id: cardBody
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            Txt {
                                text: (card.onBreak ? "󰒲  " : "󰒃  ") + card.modelData.name
                                font.pixelSize: 18
                                font.bold: true
                                color: card.onBreak ? theme.success : theme.accent
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Txt { visible: card.modelData.locked; text: "󰌾 " + qsTr("locked"); color: theme.warning }
                        }
                        Txt {
                            text: { backend.now; return card.onBreak ? backend.remaining(card.modelData.suspendedUntil)
                                                                     : card.modelData.endsAt ? backend.remaining(card.modelData.endsAt) : "∞" }
                            font.pixelSize: 44
                            font.bold: true
                        }
                        Txt {
                            dimmed: true
                            text: card.onBreak ? qsTr("on a %1 until %2").arg(card.evaluation.suspendKind === "pass" ? qsTr("pass") : qsTr("break")).arg(backend.clock(card.modelData.suspendedUntil))
                                : (card.modelData.endsAt ? qsTr("until %1").arg(backend.clock(card.modelData.endsAt)) : qsTr("until you stop it"))
                                  + " · " + ({ schedule: qsTr("scheduled"), session: qsTr("session"), limit: qsTr("limit reached"), trigger: qsTr("triggered") })[card.modelData.reason]
                        }
                        Flow {
                            Layout.fillWidth: true
                            Layout.topMargin: 6
                            spacing: 6
                            VButton {
                                visible: !card.onBreak && card.evaluation.breaks && card.evaluation.breaks.left !== undefined
                                enabledState: card.evaluation.breaks && card.evaluation.breaks.left > 0
                                glyph: "󰒲"
                                text: qsTr("Break · %1 left").arg(backend.duration(card.evaluation.breaks ? card.evaluation.breaks.left : 0))
                                onClicked: root.act(backend.takeBreak(card.modelData.id), qsTr("Break started"))
                            }
                            VButton {
                                visible: !card.onBreak && card.evaluation.passes && card.evaluation.passes.left !== undefined
                                enabledState: card.evaluation.passes && card.evaluation.passes.left > 0
                                glyph: "󰈀"
                                text: qsTr("Pass · %1 left today").arg(card.evaluation.passes ? card.evaluation.passes.left : 0)
                                onClicked: root.act(backend.usePass(card.modelData.id), qsTr("Pass started"))
                            }
                            VButton {
                                visible: card.onBreak && card.evaluation.suspendKind === "break"
                                text: qsTr("Back to work")
                                primary: true
                                onClicked: root.act(backend.endBreak(card.modelData.id), qsTr("Break ended"))
                            }
                            VButton {
                                visible: card.modelData.reason === "session" && !card.modelData.locked && !(card.evaluation.session && card.evaluation.session.locked)
                                text: qsTr("Stop session")
                                onClicked: root.act(backend.stopSession(card.modelData.id), qsTr("Session stopped"))
                            }
                            VButton { text: qsTr("Edit"); glyph: "󰏫"; onClicked: root.editBlock(card.modelData.id) }
                        }
                    }
                }
            }

            // ---------------------------------------------- quick start
            Heading { text: qsTr("Block now"); hint: qsTr("Start one of your blocks for a while, on top of its schedule. Space starts it.") }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: backend.blocks
                    Chip {
                        text: modelData.name
                        checked: root.quickBlock === modelData.id || (root.quickBlock === "" && index === 0)
                        onToggled: root.quickBlock = modelData.id
                    }
                }
                Txt { visible: backend.blocks.length === 0; text: qsTr("No blocks yet."); dimmed: true }
            }
            RowLayout {
                spacing: 4
                Repeater {
                    model: [25, 50, 90, 120, 240]
                    Chip {
                        minWidth: 52
                        text: backend.inMinutes(modelData)
                        checked: root.quickMinutes === modelData
                        onToggled: { root.quickMinutes = modelData; customField.text = "" }
                    }
                }
                VField {
                    id: customField
                    fieldWidth: 110
                    placeholder: qsTr("or 1h45m")
                    valid: text === "" || backend.parseMinutes(text) > 0
                    onEdited: if (backend.parseMinutes(text) > 0) root.quickMinutes = backend.parseMinutes(text)
                    onAccepted: root.startQuick()
                }
                Item { width: 10; height: 1 }
                Chip {
                    text: "󰌾 " + qsTr("Lock it")
                    checked: root.quickLock
                    onToggled: root.quickLock = !root.quickLock
                }
                Item { width: 6; height: 1 }
                VButton { text: qsTr("Start"); glyph: "󰐊"; primary: true; enabledState: backend.online && backend.blocks.length > 0; onClicked: root.startQuick() }
            }

            // ---------------------------------------------- limits
            Heading { visible: root.limited.length > 0; text: qsTr("Limits") }
            Repeater {
                model: root.limited
                RowLayout {
                    required property var modelData
                    readonly property var evaluation: backend.evaluations[modelData.id] || {}
                    Layout.fillWidth: true
                    spacing: 12
                    Txt { text: modelData.name; Layout.preferredWidth: 180; elide: Text.ElideRight }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 8
                        radius: Math.min(4, theme.radius)
                        color: theme.surfaceHigh
                        Rectangle {
                            width: parent.width * Math.min(1, evaluation.limitTotal > 0 ? evaluation.limitUsed / evaluation.limitTotal : 0)
                            height: parent.height
                            radius: parent.radius
                            color: evaluation.blocking ? theme.danger : theme.accent
                        }
                    }
                    Txt {
                        Layout.preferredWidth: 170
                        horizontalAlignment: Text.AlignRight
                        dimmed: !evaluation.blocking
                        color: evaluation.blocking ? theme.danger : theme.dim
                        text: evaluation.inWindow ? qsTr("%1 of %2").arg(backend.duration(evaluation.limitUsed || 0)).arg(backend.inMinutes(modelData.limit.minutes))
                                                  : qsTr("not counting now")
                    }
                }
            }

            // ---------------------------------------------- upcoming
            Heading { visible: root.upcoming.length > 0; text: qsTr("Coming up") }
            Repeater {
                model: root.upcoming
                RowLayout {
                    spacing: 12
                    Txt { text: backend.clock(modelData.at); Layout.preferredWidth: 180; color: theme.accent }
                    Txt { text: modelData.name }
                }
            }
        }
    }

    ConfirmDialog {
        id: confirmQuickLock
        title: qsTr("Lock for %1?").arg(backend.inMinutes(root.quickMinutes))
        message: qsTr("The session cannot be stopped, and the block cannot be loosened, until it ends. There is no early unlock.")
        confirmText: qsTr("Start locked")
        onConfirmed: {
            var id = root.quickBlock !== "" ? root.quickBlock : backend.blocks[0].id
            var result = backend.startSession(id, root.quickMinutes, true)
            root.toast(result === "" ? qsTr("Locked for %1").arg(backend.inMinutes(root.quickMinutes)) : result)
        }
    }
}
