import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1180
    height: 560
    minimumWidth: 960
    minimumHeight: 460
    visible: true
    title: "KLC Light"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 12

            Pane { id: layersPane; Layout.preferredWidth: 220; Layout.fillHeight: true }
            Pane { id: keyboardPane; Layout.fillWidth: true; Layout.fillHeight: true }
            Pane { id: settingsPane; Layout.preferredWidth: 280; Layout.fillHeight: true }
        }
    }
}
