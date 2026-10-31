import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1280
    height: 640
    minimumWidth: 900
    // Last group button used, and the selection it made (JSON), to show it as active.
    property string activeGroup: ""
    property string groupSelection: ""
    minimumHeight: 480
    visible: true
    title: "KLC Light"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    // Set once the user chose to discard unapplied changes, so closing goes through.
    property bool discarding: false

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: win.requestQuit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: backend.apply() }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; onActivated: backend.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; context: Qt.ApplicationShortcut; onActivated: backend.redo() }
    Shortcut { sequence: "Esc"; context: Qt.ApplicationShortcut; onActivated: { win.helpVisible = false; backend.clearSelection() } }

    function requestQuit() {
        if (backend.dirty)
            quitDialog.open()
        else
            Qt.quit()
    }

    onClosing: close => {
        if (backend.dirty && !win.discarding) {
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
                    ToolButton {
                        text: "×"
                        onClicked: backend.dismissError()
                        ToolTip.text: qsTr("Dismiss")
                        ToolTip.visible: hovered
                    }
                }
            }

            // The keyboard is three times wider than tall: it gets the whole width, and the
            // layers and settings sit below it.
            Pane {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(width * 0.36, win.height * 0.45)
                padding: 6
                KeyboardView { anchors.fill: parent }
            }

            RowLayout {
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
                    // Real buttons (they read as plain text when flat); the group whose keys are
                    // exactly the current selection shows as active.
                    delegate: Button {
                        required property var modelData
                        text: modelData.text
                        highlighted: win.activeGroup === modelData.group
                                     && JSON.stringify(backend.selection) === win.groupSelection
                        Material.elevation: 0
                        Material.roundedScale: theme.radius
                        onClicked: {
                            backend.selectGroup(modelData.group)
                            win.activeGroup = modelData.group
                            win.groupSelection = JSON.stringify(backend.selection)
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    opacity: 0.5
                    text: qsTr("Click, Ctrl+click, drag · ? for help")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12

                Pane {
                    Layout.preferredWidth: 240
                    Layout.fillHeight: true
                    LayerList { anchors.fill: parent }
                }

                Pane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ScrollView {
                        anchors.fill: parent
                        contentWidth: availableWidth
                        // Nothing scrolls sideways; the hidden horizontal bar still caught clicks
                        // on the bottom 16 px of the panel.
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        EffectPanel { width: parent.width }
                    }
                }
            }

            // Always on screen, whatever the window height: only the panel above scrolls.
            RowLayout {
                objectName: "actionBar"
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    font.pixelSize: 11
                    opacity: 0.7
                    text: backend.busy ? qsTr("Sending…")
                         : backend.cooldownMs > 0 ? qsTr("Ready again in %1 s").arg(Math.ceil(backend.cooldownMs / 1000))
                         : backend.dirty ? qsTr("Unapplied changes") : ""
                }
                Button {
                    objectName: "undoButton"
                    text: qsTr("Undo")
                    flat: true
                    enabled: backend.canUndo
                    onClicked: backend.undo()
                    ToolTip.text: "Ctrl+Z"
                    ToolTip.visible: hovered
                }
                Button {
                    objectName: "redoButton"
                    text: qsTr("Redo")
                    flat: true
                    enabled: backend.canRedo
                    onClicked: backend.redo()
                    ToolTip.text: "Ctrl+Shift+Z"
                    ToolTip.visible: hovered
                }
                Button {
                    objectName: "applyButton"
                    text: qsTr("Apply")
                    highlighted: true
                    enabled: !backend.busy && backend.cooldownMs === 0
                    onClicked: backend.apply()
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
                            [qsTr("Click"), qsTr("Select a key")],
                            [qsTr("Ctrl+click"), qsTr("Add a key to the selection")],
                            [qsTr("Drag"), qsTr("Select a rectangle of keys")],
                            ["Space", qsTr("Apply to the keyboard (nothing is sent before)")],
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
        objectName: "quitDialog"
        anchors.centerIn: parent
        modal: true
        title: qsTr("Quit without applying?")
        Label { text: qsTr("Your changes have not been applied to the keyboard or saved.") }
        standardButtons: Dialog.Discard | Dialog.Cancel
        // Same reason as FlatComboBox: Material's elevation effect hides the background.
        background: Rectangle {
            color: Material.dialogColor
            radius: Math.min(4, theme.radius)
            border.color: Qt.rgba(1, 1, 1, 0.12)
        }
        onDiscarded: {
            win.discarding = true
            quitDialog.close()
            win.close()
        }
    }
}
