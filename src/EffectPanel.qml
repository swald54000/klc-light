import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Settings of the selected layer, its color, speed and brightness, and Apply: three
// columns under the keyboard, or one when the window is narrow.
GridLayout {
    id: panel

    readonly property var cur: backend.current
    readonly property string kind: backend.currentKind
    // Index of the step whose color the picker edits.
    property int stepIndex: 0

    columns: width > 760 ? 3 : 1
    columnSpacing: 24
    rowSpacing: 8

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        Layout.preferredWidth: 1
        spacing: 8
        Label { text: qsTr("EFFECT"); font.pixelSize: 11; opacity: 0.6 }

        FlatComboBox {
            objectName: "baseTypeBox"
            Layout.fillWidth: true
            visible: panel.kind === "base"
            model: [
                { value: "static", text: qsTr("Static") },
                { value: "breathing", text: qsTr("Breathing") },
                { value: "flashing", text: qsTr("Flashing") },
                { value: "rainbow", text: qsTr("Rainbow") },
                { value: "wave", text: qsTr("Wave") }
            ]
            textRole: "text"
            valueRole: "value"
            currentIndex: Math.max(0, model.findIndex(m => m.value === panel.cur.type))
            onActivated: backend.setBaseType(currentValue)
        }

        Label {
            Layout.fillWidth: true
            visible: panel.kind === "keys"
            text: backend.selection.length === 0 ? qsTr("Select keys, then pick a color.")
                                                 : qsTr("%n key(s) selected", "", backend.selection.length)
            wrapMode: Text.WordWrap
            opacity: 0.8
        }

        Label {
            Layout.fillWidth: true
            visible: panel.kind === "effect"
            text: panel.cur.name || ""
            font.bold: true
        }

        // Color cycle of a wave or pulse: one row per step.
        ColumnLayout {
            Layout.fillWidth: true
            visible: (panel.kind === "effect" || (panel.kind === "base" && panel.cur.type === "wave"))
            spacing: 2
            Repeater {
                model: panel.cur.steps || []
                delegate: RowLayout {
                    required property int index
                    required property var modelData
                    Layout.fillWidth: true
                    Rectangle {
                        width: 22; height: 22
                        radius: Math.min(4, theme.radius)
                        color: modelData.color
                        border.width: index === panel.stepIndex ? 2 : 1
                        border.color: index === panel.stepIndex ? "white" : Qt.rgba(1, 1, 1, 0.2)
                        MouseArea { anchors.fill: parent; onClicked: panel.stepIndex = index }
                    }
                    SpinBox {
                        Layout.fillWidth: true
                        from: 1; to: 9999
                        editable: true
                        value: modelData.duration
                        onValueModified: backend.setStepDuration(index, value)
                        ToolTip.text: qsTr("Duration in controller units")
                        ToolTip.visible: hovered
                    }
                    ToolButton { text: "×"; onClicked: backend.removeStep(index) }
                }
            }
            RowLayout {
                Button { text: qsTr("+ Step"); onClicked: backend.addStep() }
                FlatComboBox {
                    Layout.fillWidth: true
                    model: ["aqua-0", "aqua-1", "aqua-2", "aqua-3", "aqua-4", "aqua-5"]
                    currentIndex: Math.max(0, model.indexOf(panel.cur.origin))
                    onActivated: backend.setOrigin(currentText)
                    ToolTip.text: qsTr("Wave origin, from the captured preset")
                    ToolTip.visible: hovered
                }
            }
            Button {
                Layout.fillWidth: true
                visible: panel.kind === "effect"
                enabled: backend.selection.length > 0
                text: qsTr("Add selected keys to this effect")
                onClicked: backend.assignSelection()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: panel.kind === "reactive"
            Switch {
                text: qsTr("Experimental: send reactive lighting")
                checked: backend.experimentalReactive
                onToggled: backend.experimentalReactive = checked
            }
            Switch {
                text: qsTr("Light keys when pressed")
                enabled: backend.experimentalReactive
                checked: panel.cur.enabled === true
                onToggled: backend.setReactiveEnabled(checked)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                opacity: 0.6
                text: qsTr("Not yet validated on this keyboard's firmware.")
            }
        }

    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        Layout.preferredWidth: 1
        spacing: 8
        Label { text: qsTr("COLOR"); font.pixelSize: 11; opacity: 0.6 }

        ColorPicker {
            Layout.fillWidth: true
            enabled: !(panel.kind === "base" && panel.cur.type === "rainbow")
            color: {
                if (panel.cur.steps && panel.cur.steps.length > 0 && (panel.kind === "effect" || panel.cur.type === "wave"))
                    return panel.cur.steps[Math.min(panel.stepIndex, panel.cur.steps.length - 1)].color
                return panel.cur.color || "white"
            }
            onPicked: c => {
                if (panel.kind === "effect" || (panel.kind === "base" && panel.cur.type === "wave"))
                    backend.setStepColor(panel.stepIndex, c)
                else
                    backend.setColor(c)
            }
        }

    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        Layout.preferredWidth: 1
        spacing: 8
        Label { text: qsTr("Speed: %1 %").arg(backend.speed); font.pixelSize: 11; opacity: 0.6 }
        Slider {
            Layout.fillWidth: true
            from: 25; to: 400; stepSize: 5
            value: backend.speed
            onMoved: backend.speed = value
        }
        Label { text: qsTr("Brightness: %1 %").arg(backend.brightness); font.pixelSize: 11; opacity: 0.6 }
        Slider {
            Layout.fillWidth: true
            from: 1; to: 100; stepSize: 1
            value: backend.brightness
            onMoved: backend.brightness = value
        }


        RowLayout {
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
                text: qsTr("Apply")
                highlighted: true
                enabled: !backend.busy && backend.cooldownMs === 0
                onClicked: backend.apply()
            }
        }

    }

    onKindChanged: stepIndex = 0
}
