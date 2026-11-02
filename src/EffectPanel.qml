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
    // The open group animates: its steps are edited, not one color.
    readonly property bool animated: kind === "group" && cur.behavior !== "static"
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
            visible: panel.kind === "selection"
            text: qsTr("%n key(s) selected", "", backend.selection.length)
            wrapMode: Text.WordWrap
            opacity: 0.8
        }

        Label {
            Layout.fillWidth: true
            visible: panel.kind === "group"
            text: panel.cur.name || ""
            font.bold: true
        }

        FlatComboBox {
            objectName: "behaviorBox"
            Layout.fillWidth: true
            visible: panel.kind === "group"
            model: [
                { value: "static", text: qsTr("Static") },
                { value: "pulse", text: qsTr("Pulse") },
                { value: "wave", text: qsTr("Wave") }
            ]
            textRole: "text"
            valueRole: "value"
            currentIndex: Math.max(0, model.findIndex(m => m.value === panel.cur.behavior))
            onActivated: {
                const problem = backend.setBehavior(currentValue)
                currentIndex = Math.max(0, model.findIndex(m => m.value === panel.cur.behavior))
                if (problem !== "")
                    behaviorProblem.text = problem
            }
        }
        Label {
            id: behaviorProblem
            Layout.fillWidth: true
            visible: panel.kind === "group" && text !== ""
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            color: "#ff8a80"
            Connections {
                target: backend
                function onCurrentChanged() { behaviorProblem.text = "" }
            }
        }

        // Color cycle of a wave or pulse: one row per step.
        ColumnLayout {
            Layout.fillWidth: true
            visible: (panel.animated || (panel.kind === "base" && panel.cur.type === "wave"))
            spacing: 2
            Repeater {
                // A count, not the steps array: the array is rebuilt on every edit, which would
                // recreate every row (and kill a held + or − button) each time.
                model: (panel.cur.steps || []).length
                delegate: RowLayout {
                    id: row
                    required property int index
                    readonly property var step: (panel.cur.steps || [])[index] || ({ color: "black", duration: 1 })
                    Layout.fillWidth: true
                    Rectangle {
                        width: 22; height: 22
                        radius: Math.min(4, theme.radius)
                        color: row.step.color
                        border.width: row.index === panel.stepIndex ? 2 : 1
                        border.color: row.index === panel.stepIndex ? "white" : Qt.rgba(1, 1, 1, 0.2)
                        MouseArea { anchors.fill: parent; onClicked: panel.stepIndex = row.index }
                    }
                    SpinBox {
                        objectName: row.index === 0 ? "stepDuration" : ""
                        Layout.fillWidth: true
                        from: 1; to: 9999
                        editable: true
                        value: row.step.duration
                        // One controller unit is 10 ms (the preview's assumption): show seconds.
                        textFromValue: function(v, locale) { return Number(v / 100).toLocaleString(locale, "f", 2) + " s" }
                        valueFromText: function(text, locale) {
                            let n = NaN
                            try { n = Number.fromLocaleString(locale, text.replace(/\s*s\s*$/, "").trim()) } catch (e) {}
                            if (isNaN(n))
                                n = Number(text.replace(",", ".").replace(/\s*s\s*$/, ""))
                            return isNaN(n) ? value : Math.round(n * 100)
                        }
                        validator: RegularExpressionValidator { regularExpression: /^\s*\d+([.,]\d{0,2})?\s*s?\s*$/ }
                        // Holding + or − is one undo step.
                        readonly property bool held: up.pressed || down.pressed
                        onHeldChanged: held ? backend.beginGesture() : backend.endGesture()
                        onValueModified: backend.setStepDuration(row.index, value)
                        ToolTip.text: qsTr("Step duration")
                        ToolTip.visible: hovered
                    }
                    ToolButton {
                        text: "×"
                        onClicked: backend.removeStep(row.index)
                        ToolTip.text: qsTr("Remove this step")
                        ToolTip.visible: hovered
                    }
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
            enabled: !(panel.kind === "base" && panel.cur.type === "rainbow") && panel.kind !== "selection"
            color: {
                if (panel.cur.steps && panel.cur.steps.length > 0 && (panel.animated || (panel.kind === "base" && panel.cur.type === "wave")))
                    return panel.cur.steps[Math.min(panel.stepIndex, panel.cur.steps.length - 1)].color
                return panel.cur.color || "white"
            }
            onPicked: c => {
                if (panel.animated || (panel.kind === "base" && panel.cur.type === "wave"))
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
            onPressedChanged: pressed ? backend.beginGesture() : backend.endGesture()
            onMoved: backend.speed = value
        }
        Label { text: qsTr("Brightness: %1 %").arg(backend.brightness); font.pixelSize: 11; opacity: 0.6 }
        Slider {
            objectName: "brightnessSlider"
            Layout.fillWidth: true
            from: 1; to: 100; stepSize: 1
            value: backend.brightness
            onPressedChanged: pressed ? backend.beginGesture() : backend.endGesture()
            onMoved: backend.brightness = value
        }




    }

    onKindChanged: stepIndex = 0
}
