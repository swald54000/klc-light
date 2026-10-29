import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1280
    height: 640
    minimumWidth: 1040
    minimumHeight: 560
    visible: true
    title: "KLC Light"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: win.requestQuit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: backend.apply() }
    Shortcut { sequence: StandardKey.Undo; context: Qt.ApplicationShortcut; onActivated: backend.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; context: Qt.ApplicationShortcut; onActivated: backend.redo() }
    Shortcut { sequence: "Esc"; context: Qt.ApplicationShortcut; onActivated: { win.helpVisible = false; backend.clearSelection() } }

    function requestQuit() {
        if (backend.dirty)
            quitDialog.open()
        else
            Qt.quit()
    }

    onClosing: close => {
        if (backend.dirty) {
            close.accepted = false
            quitDialog.open()
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Pane {
                Layout.fillWidth: true
                visible: backend.error !== "" || backend.notice !== ""
                Material.background: backend.error !== "" ? "#4a1418" : "#3a3014"
                Material.roundedScale: theme.radius
                RowLayout {
                    anchors.fill: parent
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: backend.error !== "" ? backend.error : backend.notice
                    }
                    ToolButton { text: "×"; onClicked: backend.dismissError() }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12

                Pane {
                    Layout.preferredWidth: 230
                    Layout.fillHeight: true
                    LayerList { anchors.fill: parent }
                }

                Pane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    ColumnLayout {
                        anchors.fill: parent
                        KeyboardView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: [
                                    { group: "wasd", text: qsTr("ZQSD / WASD") },
                                    { group: "function", text: qsTr("F1–F12") },
                                    { group: "digits", text: qsTr("Digits") },
                                    { group: "numpad", text: qsTr("Numpad") },
                                    { group: "modifiers", text: qsTr("Modifiers") },
                                    { group: "all", text: qsTr("All") }
                                ]
                                delegate: Button {
                                    required property var modelData
                                    flat: true
                                    text: modelData.text
                                    onClicked: backend.selectGroup(modelData.group)
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            opacity: 0.5
                            text: qsTr("Click selects a key, Ctrl+click adds, drag selects a rectangle. Nothing reaches the keyboard until Apply.")
                        }
                    }
                }

                Pane {
                    Layout.preferredWidth: 300
                    Layout.fillHeight: true
                    ScrollView {
                        anchors.fill: parent
                        contentWidth: availableWidth
                        EffectPanel { width: parent.width }
                    }
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: Qt.rgba(0, 0, 0, 0.75)
            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }
            Pane {
                anchors.centerIn: parent
                Material.roundedScale: theme.radius
                GridLayout {
                    columns: 2
                    columnSpacing: 24
                    Repeater {
                        model: [
                            ["Space", qsTr("Apply to the keyboard")],
                            ["Ctrl+Z", qsTr("Undo")],
                            ["Ctrl+Shift+Z", qsTr("Redo")],
                            ["Esc", qsTr("Clear the selection")],
                            ["?", qsTr("Show or hide this help")],
                            ["Q", qsTr("Quit")]
                        ]
                        delegate: Label {
                            required property var modelData
                            required property int index
                            Layout.row: index
                            Layout.column: 0
                            text: modelData[0]
                            font.bold: true
                        }
                    }
                    Repeater {
                        model: [qsTr("Apply to the keyboard"), qsTr("Undo"), qsTr("Redo"), qsTr("Clear the selection"),
                                qsTr("Show or hide this help"), qsTr("Quit")]
                        delegate: Label {
                            required property string modelData
                            required property int index
                            Layout.row: index
                            Layout.column: 1
                            text: modelData
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: quitDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Quit without applying?")
        Label { text: qsTr("Your changes have not been applied to the keyboard or saved.") }
        standardButtons: Dialog.Discard | Dialog.Cancel
        onDiscarded: Qt.quit()
    }
}
