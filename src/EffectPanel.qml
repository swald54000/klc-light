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
    // Index of the step whose color the picker edits: the first one of each newly opened
    // group or base.
    property int stepIndex: 0

    function keyLabel(id) {
        const key = backend.keys.find(k => k.id === id)
        return key ? key.label : (id || "")
    }
    readonly property int group: backend.currentGroup
    onGroupChanged: stepIndex = 0
    // A removed step may have been the selected one: keep to the steps that remain.
    readonly property int stepCount: (cur.steps || []).length
    onStepCountChanged: if (stepCount > 0 && stepIndex >= stepCount) stepIndex = stepCount - 1

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

        // Free keys: make a group of them, or add them to one.
        ColumnLayout {
            Layout.fillWidth: true
            visible: panel.kind === "selection"
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: qsTr("%n key(s) selected", "", backend.selection.length)
                wrapMode: Text.WordWrap
                opacity: 0.8
            }
            Label { text: qsTr("New group"); font.pixelSize: 11; opacity: 0.6 }
            TextField {
                id: newGroupName
                objectName: "newGroupName"
                Layout.fillWidth: true
                // The default follows the selection (the key's label for a single key).
                text: backend.selection.length >= 0 ? backend.defaultGroupName() : ""
                onTextEdited: newGroupError.text = ""
                onAccepted: createGroupButton.clicked()
            }
            Button {
                id: createGroupButton
                objectName: "createGroupButton"
                Layout.fillWidth: true
                text: qsTr("Create group")
                onClicked: newGroupError.text = backend.createGroup(newGroupName.text)
            }
            Label {
                id: newGroupError
                objectName: "newGroupError"
                Layout.fillWidth: true
                textFormat: Text.PlainText
                visible: text !== ""
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                color: "#ff8a80"
                Connections {
                    target: backend
                    function onSelectionChanged() { newGroupError.text = "" }
                }
            }
            FlatComboBox {
                objectName: "addToGroupBox"
                Layout.fillWidth: true
                visible: backend.groups.length > 0
                model: backend.groups.map(g => g.name)
                currentIndex: -1
                displayText: qsTr("Add to group")
                onActivated: index => backend.addSelectionToGroup(index)
            }
        }

        TextField {
            id: groupName
            objectName: "groupNameField"
            Layout.fillWidth: true
            visible: panel.kind === "group"
            font.bold: true
            readonly property string shown: panel.cur.name || ""
            text: shown
            onShownChanged: { text = shown; groupNameError.text = "" }
            onTextEdited: groupNameError.text = ""
            // Renaming happens on Enter or when the field loses focus; a refusal is shown
            // under the field and the typed text kept for correction.
            onEditingFinished: {
                if (text !== shown)
                    groupNameError.text = backend.renameGroup(backend.currentGroup, text)
            }
        }
        Label {
            id: groupNameError
            objectName: "groupNameError"
            Layout.fillWidth: true
            textFormat: Text.PlainText
            visible: text !== ""
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            color: "#ff8a80"
        }

        FlatComboBox {
            id: behaviorBox
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
                behaviorProblem.text = backend.setBehavior(currentValue)
                // A refused change leaves the group as it was: show that, and keep the binding.
                currentIndex = Qt.binding(() => Math.max(0, model.findIndex(m => m.value === panel.cur.behavior)))
            }
        }
        Label {
            id: behaviorProblem
            objectName: "behaviorError"
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
            }
        }

        // Where a wave runs: to an edge or the centre, or toward or away from a picked key.
        ColumnLayout {
            Layout.fillWidth: true
            visible: (panel.kind === "group" && panel.cur.behavior === "wave") || (panel.kind === "base" && panel.cur.type === "wave")
            spacing: 4
            FlatComboBox {
                objectName: "waveDirectionBox"
                Layout.fillWidth: true
                model: [
                    { value: "left", text: qsTr("Toward the left") },
                    { value: "right", text: qsTr("Toward the right") },
                    { value: "up", text: qsTr("Upward") },
                    { value: "down", text: qsTr("Downward") },
                    { value: "center-in", text: qsTr("Toward the centre") },
                    { value: "center-out", text: qsTr("Out from the centre") },
                    { value: "key-in", text: qsTr("Toward a key") },
                    { value: "key-out", text: qsTr("Out from a key") }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: Math.max(0, model.findIndex(m => m.value === panel.cur.direction))
                onActivated: backend.setWaveDirection(currentValue)
                ToolTip.text: qsTr("Wave direction")
                ToolTip.visible: hovered
            }
            RowLayout {
                Layout.fillWidth: true
                visible: panel.cur.direction === "key-in" || panel.cur.direction === "key-out"
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Key: %1").arg(panel.keyLabel(panel.cur.waveKey))
                    elide: Text.ElideRight
                }
                Button {
                    objectName: "pickOriginButton"
                    text: qsTr("Pick the key")
                    highlighted: backend.picking
                    onClicked: backend.picking ? backend.cancelPick() : backend.pickWaveOrigin()
                }
            }
            Label {
                objectName: "pickHint"
                Layout.fillWidth: true
                visible: backend.picking
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                color: Material.accent
                text: qsTr("Click a key to set the wave origin · Esc cancels")
            }
            Label { text: qsTr("Wave size: %1").arg(panel.cur.waveSize || 68); font.pixelSize: 11; opacity: 0.6 }
            Slider {
                objectName: "waveSizeSlider"
                Layout.fillWidth: true
                from: 1; to: 100; stepSize: 1
                value: panel.cur.waveSize || 68
                onPressedChanged: pressed ? backend.beginGesture() : backend.endGesture()
                onMoved: backend.setWaveSize(value)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: panel.kind === "group"
            Button {
                objectName: "removeKeysButton"
                Layout.fillWidth: true
                text: qsTr("Remove selected keys")
                enabled: backend.selection.length > 0
                onClicked: backend.removeSelectionFromGroup()
            }
            Button {
                objectName: "deleteGroupButton"
                Layout.fillWidth: true
                text: qsTr("Delete group")
                onClicked: backend.deleteGroup(backend.currentGroup)
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: panel.kind === "reactive"
            Switch {
                objectName: "reactiveSwitch"
                text: qsTr("Light keys when pressed")
                checked: panel.cur.enabled === true
                onToggled: backend.setReactiveEnabled(checked)
            }
            Label {
                text: qsTr("Duration: %1 ms").arg(panel.cur.duration || 300)
                font.pixelSize: 11
                opacity: 0.6
            }
            Slider {
                objectName: "reactiveDurationSlider"
                Layout.fillWidth: true
                from: 50; to: 2000; stepSize: 10
                value: panel.cur.duration || 300
                onPressedChanged: pressed ? backend.beginGesture() : backend.endGesture()
                onMoved: backend.setReactiveDuration(value)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                opacity: 0.6
                // The keyboard's LED matrix leaks some green into a neighbouring key (measured).
                text: qsTr("Colors with little green avoid a faint glow on a neighbouring key.")
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
