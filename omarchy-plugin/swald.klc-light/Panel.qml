import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Commons as Commons
import qs.Ui

// Keyboard lighting from KLC Light: the applied profile in the bar, the library in a panel.
// It only talks to the keyboard through the klc-light command line, which keeps one send at
// a time; the list follows state.json, so changes made in the editor or by theme hooks show.
Panel {
  id: root
  moduleName: "swald.klc-light"
  ipcTarget: "swald.klc-light"

  property var library: ({})     // `klc-light list --json`
  property bool available: true  // klc-light answered `list --json` with a library
  property bool busy: false      // a send is running
  property string error: ""
  readonly property bool off: library.off === true
  readonly property string applied: library.applied || ""
  readonly property string currentTheme: library.currentTheme || ""
  readonly property var profiles: Array.isArray(library.profiles) ? library.profiles : []

  Commons.Translations { id: strings; directory: Qt.resolvedUrl("i18n") }

  function refresh() {
    if (!listProc.running) listProc.running = true
  }

  // One klc-light command at a time; the list is read again when it ends.
  function send(args) {
    if (busy || !available) return
    busy = true
    error = ""
    actionProc.command = ["klc-light"].concat(args).concat(["--json"])
    actionProc.running = true
  }

  Component.onCompleted: refresh()
  onOpenedChanged: if (opened) refresh()

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  Process {
    id: listProc
    command: ["klc-light", "list", "--json"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        try {
          var parsed = JSON.parse(text)
          if (!parsed || !Array.isArray(parsed.profiles)) throw "no library"
          root.library = parsed
          root.available = true
        } catch (e) {
          // Not installed, or a version without `list`.
          root.available = false
        }
      }
    }
    onExited: function(code) { if (code !== 0) root.available = false }
  }

  Process {
    id: actionProc
    stdout: StdioCollector { id: actionOut; waitForEnd: true }
    onExited: function(code) {
      root.busy = false
      if (code === 2) {
        root.error = strings.t("error.absent")
      } else if (code !== 0) {
        var message = ""
        try { message = JSON.parse(actionOut.text).error || "" } catch (e) {}
        root.error = message !== "" ? message : strings.t("error.failed")
      }
      root.refresh()
    }
  }

  // Written by every send, from here, the editor or the Omarchy hooks.
  FileView {
    path: Quickshell.env("HOME") + "/.config/klc-light/state.json"
    watchChanges: true
    printErrors: false
    onFileChanged: { reload(); root.refresh() }
  }

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: "󰌌"
    dimmed: root.off || !root.available
    tooltipText: (!root.available ? strings.t("unavailable")
                  : root.off ? strings.t("tooltip.off")
                  : root.applied !== "" ? root.applied : strings.t("tooltip.none"))
                 + "\n" + strings.t("tooltip.clicks")
    onPressed: function(b) {
      if (b === Qt.RightButton) root.send(["off"])
      else if (b === Qt.MiddleButton) root.send(["apply"])
      else root.toggle()
    }
  }

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: keyCatcher
    contentWidth: panel.fittedContentWidth(Style.space(320))
    contentHeight: panel.fittedContentHeight(column.implicitHeight)

    PanelKeyCatcher {
      id: keyCatcher
      anchors.fill: parent
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }

      Column {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: Style.space(10)

        Text {
          text: strings.t("panel.title")
          color: root.bar ? root.bar.foreground : Color.foreground
          font.family: root.bar ? root.bar.fontFamily : Style.font.family
          font.pixelSize: Style.font.title
          font.bold: true
        }

        Text {
          visible: !root.available
          width: parent.width
          wrapMode: Text.WordWrap
          text: strings.t("unavailable") + "\nhttps://github.com/swald54000/klc-light"
          color: root.bar ? root.bar.foreground : Color.foreground
          font.family: root.bar ? root.bar.fontFamily : Style.font.family
          font.pixelSize: Style.font.bodySmall
        }

        PanelSectionHeader {
          visible: root.available
          text: strings.t("panel.profiles")
          foreground: root.bar ? root.bar.foreground : Color.foreground
          fontFamily: root.bar ? root.bar.fontFamily : Style.font.family
        }

        Text {
          visible: root.available && root.profiles.length === 0
          text: strings.t("panel.empty")
          color: root.bar ? root.bar.foreground : Color.foreground
          font.family: root.bar ? root.bar.fontFamily : Style.font.family
          font.pixelSize: Style.font.bodySmall
        }

        Repeater {
          model: root.available ? root.profiles : []
          Button {
            required property var modelData
            readonly property bool linked: modelData.theme !== "" && modelData.theme === root.currentTheme
            width: column.width
            leftAlign: true
            iconText: modelData.name === root.applied ? "󰄬" : ""
            text: modelData.name
                  + (linked ? "  󰌹 " + strings.t("panel.linked") : "")
                  + (modelData.unreadable ? " (" + strings.t("panel.unreadable") + ")" : "")
            selected: modelData.name === root.applied && !root.off
            enabled: !modelData.unreadable && !root.busy
            opacity: enabled ? 1 : 0.5
            fontSize: Style.font.bodySmall
            foreground: root.bar ? root.bar.foreground : Color.foreground
            fontFamily: root.bar ? root.bar.fontFamily : Style.font.family
            onClicked: root.send(["apply", modelData.name])
          }
        }

        PanelSeparator {
          visible: root.available
          foreground: root.bar ? root.bar.foreground : Color.foreground
        }

        Row {
          visible: root.available
          width: parent.width
          spacing: Style.space(6)

          Button {
            width: (parent.width - parent.spacing) / 2
            iconText: "󰏫"
            text: strings.t("panel.editor")
            bordered: true
            fontSize: Style.font.bodySmall
            foreground: root.bar ? root.bar.foreground : Color.foreground
            fontFamily: root.bar ? root.bar.fontFamily : Style.font.family
            onClicked: {
              if (root.bar) root.bar.run("uwsm-app -- klc-light")
              root.close()
            }
          }

          Button {
            width: (parent.width - parent.spacing) / 2
            iconText: root.off ? "󰌌" : "󰌐"
            text: root.off ? strings.t("panel.on") : strings.t("panel.off")
            bordered: true
            enabled: !root.busy
            fontSize: Style.font.bodySmall
            foreground: root.bar ? root.bar.foreground : Color.foreground
            fontFamily: root.bar ? root.bar.fontFamily : Style.font.family
            onClicked: root.send(root.off ? ["apply"] : ["off"])
          }
        }

        Text {
          visible: root.error !== ""
          width: parent.width
          wrapMode: Text.WordWrap
          text: root.error
          color: Color.urgent
          font.family: root.bar ? root.bar.fontFamily : Style.font.family
          font.pixelSize: Style.font.caption
        }
      }
    }
  }
}
