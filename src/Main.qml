import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1180
    height: 800
    minimumWidth: 860
    minimumHeight: 560
    visible: true
    title: "Vow"
    color: theme.background
    font.family: "monospace"

    Material.theme: theme.light ? Material.Light : Material.Dark
    Material.accent: theme.accent
    Material.background: theme.background
    Material.foreground: theme.foreground
    Material.roundedScale: theme.radius

    property int page: 0
    property bool helpVisible: false
    property string toastText: ""

    // Used by VOW_SCREENSHOT to open a view before the capture.
    function demo(action) {
        var parts = String(action).split(":")
        if (parts[0] === "open") { page = 1; blocksPage.open(parts[1]) }
        if (parts[0] === "help") helpVisible = true
    }

    function toast(text) { toastText = text; toastTimer.restart() }
    function quit() { if (blocksPage.dirty) confirmQuit.open(); else Qt.quit() }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: win.quit() }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; onActivated: win.page = 0 }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; onActivated: win.page = 1 }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; onActivated: win.page = 2 }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; onActivated: { win.page = 1; blocksPage.newBlock() } }
    Shortcut { sequences: [StandardKey.Save]; context: Qt.ApplicationShortcut; onActivated: if (win.page === 1) blocksPage.save() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: if (win.page === 0) nowPage.startQuick() }
    Shortcut { sequence: "Esc"; context: Qt.ApplicationShortcut; onActivated: { win.helpVisible = false; win.contentItem.forceActiveFocus() } }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // ------------------------------------------------ sidebar
            Rectangle {
                Layout.preferredWidth: 200
                Layout.fillHeight: true
                color: theme.surface

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 4

                    RowLayout {
                        spacing: 10
                        Layout.bottomMargin: 22
                        Txt { text: "󰒃"; font.pixelSize: 24; color: theme.accent }
                        Txt { text: "vow"; font.pixelSize: 22; font.bold: true }
                    }

                    Repeater {
                        model: [["󰥔", qsTr("Now"), "1"], ["󰒃", qsTr("Blocks"), "2"], ["󰄨", qsTr("Usage"), "3"]]
                        Rectangle {
                            Layout.fillWidth: true
                            height: 34
                            radius: Math.min(4, theme.radius)
                            color: win.page === index ? theme.surfaceHigh : navMouse.containsMouse ? Qt.rgba(theme.surfaceHigh.r, theme.surfaceHigh.g, theme.surfaceHigh.b, 0.5) : "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 10
                                Txt { text: modelData[0]; color: win.page === index ? theme.accent : theme.dim }
                                Txt { text: modelData[1]; Layout.fillWidth: true; color: win.page === index ? theme.foreground : theme.dim }
                                Txt { text: modelData[2]; dimmed: true; font.pixelSize: 11 }
                            }
                            MouseArea { id: navMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.page = index }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    // What the service can see, so a missing piece is never silent.
                    Repeater {
                        model: [
                            [backend.online, qsTr("Service"), backend.online ? qsTr("running") : qsTr("stopped")],
                            [backend.hyprland, qsTr("Hyprland"), backend.hyprland ? qsTr("connected") : qsTr("not found")],
                            [backend.extension, qsTr("Chromium"), backend.extension ? qsTr("extension on") : qsTr("no extension")]
                        ]
                        RowLayout {
                            spacing: 8
                            Rectangle { width: 7; height: 7; radius: 3.5; color: modelData[0] ? theme.success : theme.danger }
                            Txt { text: modelData[1]; font.pixelSize: 11 }
                            Txt { text: modelData[2]; font.pixelSize: 11; dimmed: true }
                        }
                    }
                    Txt { Layout.topMargin: 10; text: qsTr("? for shortcuts"); font.pixelSize: 11; dimmed: true }
                }
            }

            Rectangle { Layout.fillHeight: true; width: 1; color: theme.muted }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    visible: !backend.online
                    Layout.fillWidth: true
                    implicitHeight: offline.implicitHeight + 24
                    color: Qt.rgba(theme.danger.r, theme.danger.g, theme.danger.b, 0.12)
                    Txt {
                        id: offline
                        anchors.fill: parent
                        anchors.margins: 12
                        wrapMode: Text.WordWrap
                        color: theme.danger
                        text: backend.offlineReason || qsTr("The Vow service is not running.")
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: win.page

                    NowPage {
                        id: nowPage
                        onToast: function(text) { win.toast(text) }
                        onEditBlock: function(id) { win.page = 1; blocksPage.open(id) }
                    }
                    BlocksPage { id: blocksPage; onToast: function(text) { win.toast(text) } }
                    StatsPage { }
                }
            }
        }

        // ------------------------------------------------ toast
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
            visible: opacity > 0
            opacity: toastTimer.running ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            width: toastLabel.implicitWidth + 32
            height: 38
            radius: Math.min(6, theme.radius)
            color: theme.surfaceHigh
            border.color: theme.accent
            Txt { id: toastLabel; anchors.centerIn: parent; text: win.toastText }
        }
        Timer { id: toastTimer; interval: 2800 }

        // ------------------------------------------------ help
        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: Qt.rgba(0, 0, 0, 0.55)
            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }
            Rectangle {
                anchors.centerIn: parent
                width: 440
                height: helpColumn.implicitHeight + 40
                color: theme.surface
                border.color: theme.accent
                radius: Math.min(8, theme.radius)
                ColumnLayout {
                    id: helpColumn
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 8
                    Txt { text: qsTr("Shortcuts"); font.bold: true; font.pixelSize: 16; Layout.bottomMargin: 6 }
                    Repeater {
                        model: [
                            ["1 2 3", qsTr("Now, Blocks, Usage")],
                            ["Space", qsTr("Start the quick block")],
                            ["N", qsTr("New block")],
                            ["Ctrl+S", qsTr("Save the block")],
                            ["Esc", qsTr("Leave a field, close this")],
                            ["?", qsTr("Show shortcuts")],
                            ["Q", qsTr("Quit (blocks keep running)")]
                        ]
                        RowLayout {
                            spacing: 16
                            Rectangle {
                                Layout.preferredWidth: 80
                                height: 24
                                radius: Math.min(4, theme.radius)
                                color: theme.surfaceHigh
                                Txt { anchors.centerIn: parent; text: modelData[0]; color: theme.accent; font.pixelSize: 12 }
                            }
                            Txt { text: modelData[1] }
                        }
                    }
                }
            }
        }
    }

    ConfirmDialog {
        id: confirmQuit
        title: qsTr("Quit without saving?")
        message: qsTr("The block you are editing has changes that are not saved.")
        confirmText: qsTr("Quit")
        danger: true
        onConfirmed: Qt.quit()
    }
}
