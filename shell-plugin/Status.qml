import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// Reads /run/vow/status.json, which the Vow service rewrites whenever what
// is blocked changes, and counts down locally between rewrites.
BarWidget {
  id: root
  moduleName: "vow.status"

  property var status: ({})
  property bool online: false
  property double nowMs: Date.now()

  readonly property var active: (status.active || [])
  readonly property var current: {
    // The block that ends last is the one to show; an endless one wins.
    var pick = null
    for (var i = 0; i < active.length; i++) {
      var a = active[i]
      if (!pick || !a.endsAt || (pick.endsAt && a.endsAt > pick.endsAt)) pick = a
    }
    return pick
  }
  readonly property bool onBreak: current !== null && current.suspended === true
  readonly property bool showWhenIdle: settings && settings.showWhenIdle !== undefined ? settings.showWhenIdle : true

  Commons.Translations { id: strings; directory: Qt.resolvedUrl("i18n") }

  function parse(iso) { return iso ? new Date(iso).getTime() : 0 }

  function short(iso) {
    var seconds = Math.max(0, Math.round((parse(iso) - nowMs) / 1000))
    var h = Math.floor(seconds / 3600), m = Math.floor((seconds % 3600) / 60)
    if (h >= 24) return Math.floor(h / 24) + "d"
    if (h > 0) return h + "h" + String(m).padStart(2, "0")
    if (m > 0) return m + "m"
    return seconds + "s"
  }

  function clock(iso) {
    return iso ? Qt.locale(Commons.I18n.formatLocale).toString(new Date(iso), "HH:mm") : ""
  }

  readonly property string label: current === null ? ""
    : onBreak ? short(current.suspendedUntil)
    : current.endsAt ? short(current.endsAt) : ""

  readonly property string tooltip: {
    if (!online) return strings.t("tooltip.offline")
    if (active.length === 0) {
      var next = status.next || {}
      return next.name ? strings.t("tooltip.next").arg(next.name).arg(clock(next.startsAt)) : strings.t("tooltip.idle")
    }
    var lines = []
    for (var i = 0; i < active.length; i++) {
      var a = active[i]
      var line = a.suspended ? strings.t("tooltip.break").arg(a.name).arg(clock(a.suspendedUntil))
                             : a.reason === "permanent" ? strings.t("tooltip.permanent").arg(a.name)
                             : strings.t("tooltip.blocking").arg(a.name).arg(a.endsAt ? clock(a.endsAt) : "—")
      if (a.locked) line += " · " + strings.t("tooltip.locked")
      lines.push(line)
    }
    return lines.join("\n")
  }

  visible: online && (current !== null || showWhenIdle)
  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  FileView {
    id: statusFile
    path: "/run/vow/status.json"
    watchChanges: true
    printErrors: false
    onFileChanged: reload()
    onLoaded: {
      try {
        root.status = JSON.parse(text())
        root.online = true
      } catch (e) {
        root.online = false
      }
    }
    onLoadFailed: root.online = false
  }

  // The file is replaced, not edited, which can drop the watch; a slow poll
  // picks it up again. The countdown itself ticks locally.
  Timer { interval: 5000; running: true; repeat: true; onTriggered: statusFile.reload() }
  Timer { interval: 1000; running: root.current !== null; repeat: true; triggeredOnStart: true; onTriggered: root.nowMs = Date.now() }

  TextMetrics { id: metrics; font.family: button.fontFamily; font.pixelSize: button.fontSize; text: root.label }

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: root.onBreak ? "󰒲" : "󰒃"
    labelText: root.vertical ? "" : root.label
    slotSize: Style.bar.statusSlot + (labelText !== "" ? metrics.advanceWidth + Style.space(4) : 0)
    dimmed: root.current === null
    tooltipText: root.tooltip
    onPressed: function(b) {
      if (root.bar) root.bar.run("omarchy-launch-or-focus vow vow")
    }
  }
}
