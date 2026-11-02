import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Shapes

// A hue ring with brightness, RGB values 0–255 and saved color slots, like Mystic Light's.
ColumnLayout {
    id: picker

    property color color: "white"
    signal picked(color color)

    spacing: 8

    // Never assign `color` here: that would remove the owner's binding. The owner updates the
    // model in onPicked and the new value flows back through its binding.
    function pick(c) {
        picker.picked(c)
    }

    Item {
        objectName: "hueRing"
        Layout.alignment: Qt.AlignHCenter
        implicitWidth: 150
        implicitHeight: 150

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeWidth: 20
                strokeColor: "transparent"
                fillColor: "transparent"
                capStyle: ShapePath.FlatCap
                PathAngleArc { centerX: 75; centerY: 75; radiusX: 64; radiusY: 64; startAngle: 0; sweepAngle: 360 }
            }
            ShapePath {
                strokeWidth: 0
                strokeColor: "transparent"
                fillGradient: ConicalGradient {
                    centerX: 75; centerY: 75; angle: 0
                    GradientStop { position: 0.000; color: "#FF0000" }
                    GradientStop { position: 0.167; color: "#FF00FF" }
                    GradientStop { position: 0.333; color: "#0000FF" }
                    GradientStop { position: 0.500; color: "#00FFFF" }
                    GradientStop { position: 0.667; color: "#00FF00" }
                    GradientStop { position: 0.833; color: "#FFFF00" }
                    GradientStop { position: 1.000; color: "#FF0000" }
                }
                PathAngleArc { centerX: 75; centerY: 75; radiusX: 74; radiusY: 74; startAngle: 0; sweepAngle: 360 }
                PathAngleArc { centerX: 75; centerY: 75; radiusX: 52; radiusY: 52; startAngle: 360; sweepAngle: -360; moveToStart: true }
            }
        }

        Rectangle {
            anchors.centerIn: parent
            width: 70; height: 70; radius: 35
            color: picker.color
            border.color: Qt.rgba(1, 1, 1, 0.2)
        }

        MouseArea {
            anchors.fill: parent
            function hueAt(mouse) {
                const dx = mouse.x - 75, dy = mouse.y - 75
                const d = Math.sqrt(dx * dx + dy * dy)
                if (d < 50 || d > 76)
                    return
                // The gradient runs red, magenta, blue… counter-clockwise from the right, i.e.
                // hue decreases counter-clockwise: hue = 1 − angle.
                let angle = Math.atan2(-dy, dx) / (2 * Math.PI)
                if (angle < 0)
                    angle += 1
                const hue = (1 - angle) % 1
                // Keep the intensity the user set, unless the color is too dark to see the hue.
                const v = picker.color.hsvValue < 0.1 ? 1 : picker.color.hsvValue
                picker.pick(Qt.hsva(hue, 1, v, 1))
            }
            // One press–drag–release is one undo step.
            onPressed: mouse => { backend.beginGesture(); hueAt(mouse) }
            onPositionChanged: mouse => hueAt(mouse)
            onReleased: backend.endGesture()
            onCanceled: backend.endGesture()
        }
    }

    RowLayout {
        Label { text: qsTr("Intensity") }
        Slider {
            Layout.fillWidth: true
            from: 0; to: 1
            value: picker.color.hsvValue
            onPressedChanged: pressed ? backend.beginGesture() : backend.endGesture()
            onMoved: picker.pick(Qt.hsva(Math.max(picker.color.hsvHue, 0), picker.color.hsvSaturation, value, 1))
        }
    }

    // Hex entry: flat, monospace, "#RRGGBB". Invalid input reverts to the current color.
    TextField {
        id: hexField
        objectName: "hexField"
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: 96
        Layout.preferredHeight: 26
        topPadding: 0; bottomPadding: 0
        horizontalAlignment: TextInput.AlignHCenter
        font.family: "monospace"
        font.pixelSize: 14
        selectByMouse: true
        maximumLength: 16
        background: Rectangle {
            color: "transparent"
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width; height: 1
                color: hexField.activeFocus ? Material.accent : Qt.rgba(1, 1, 1, 0.25)
            }
        }
        function format(c) {
            const h = v => ("0" + Math.round(v * 255).toString(16)).slice(-2)
            return ("#" + h(c.r) + h(c.g) + h(c.b)).toUpperCase()
        }
        function sync() { text = format(picker.color) }
        Keys.onShortcutOverride: event => { if (event.key === Qt.Key_Escape) event.accepted = true }
        Keys.onEscapePressed: { sync(); focus = false }
        Component.onCompleted: sync()
        Connections {
            target: picker
            function onColorChanged() { hexField.sync() }
        }
        onEditingFinished: {
            const m = /^\s*#?([0-9a-fA-F]{6})\s*$/.exec(text)
            if (m && format(picker.color) !== "#" + m[1].toUpperCase())
                picker.pick("#" + m[1])
            sync()
        }
    }

    // R, G, B: gradient track from 0 to 255 of that channel, value to the right, click it to type.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0
        Repeater {
            model: [qsTr("R"), qsTr("G"), qsTr("B")]
            delegate: RowLayout {
                id: channelRow
                required property int index
                required property string modelData
                readonly property int channel: Math.round([picker.color.r, picker.color.g, picker.color.b][index] * 255)
                property bool editing: false
                property int before: 0
                spacing: 6
                Layout.fillWidth: true
                Layout.preferredHeight: 22

                function setChannel(v) {
                    const c = [picker.color.r, picker.color.g, picker.color.b]
                    c[index] = Math.max(0, Math.min(255, v)) / 255
                    picker.pick(Qt.rgba(c[0], c[1], c[2], 1))
                }
                function startEdit() {
                    before = channel
                    backend.beginGesture()
                    editing = true
                    edit.text = String(channel)
                    edit.forceActiveFocus()
                    edit.selectAll()
                }
                function endEdit(commit) {
                    if (!editing)
                        return
                    editing = false
                    if (commit) {
                        if (/^\s*\d{1,3}\s*$/.test(edit.text))
                            setChannel(parseInt(edit.text))
                        else
                            setChannel(before)
                    } else {
                        setChannel(before)
                    }
                    backend.endGesture()
                }

                Label {
                    objectName: "rgbLabel" + channelRow.index
                    text: channelRow.modelData
                    opacity: 0.7
                    Layout.preferredWidth: 12
                }
                Slider {
                    id: slider
                    objectName: "rgbSlider" + channelRow.index
                    Layout.fillWidth: true
                    Layout.preferredHeight: 22
                    from: 0; to: 255; stepSize: 1
                    topPadding: 0; bottomPadding: 0
                    value: channelRow.channel
                    onPressedChanged: pressed ? backend.beginGesture() : backend.endGesture()
                    onMoved: channelRow.setChannel(Math.round(value))
                    background: Rectangle {
                        x: slider.leftPadding + slider.handle.width / 2
                        y: slider.topPadding + slider.availableHeight / 2 - height / 2
                        width: slider.availableWidth - slider.handle.width
                        height: 8; radius: 4
                        function at(v) {
                            const c = [picker.color.r, picker.color.g, picker.color.b]
                            c[channelRow.index] = v
                            return Qt.rgba(c[0], c[1], c[2], 1)
                        }
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: slider.background.at(0) }
                            GradientStop { position: 1; color: slider.background.at(1) }
                        }
                    }
                    handle: Rectangle {
                        x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                        y: slider.topPadding + slider.availableHeight / 2 - height / 2
                        width: 14; height: 14; radius: 7
                        color: picker.color
                        border.width: 2
                        border.color: "white"
                    }
                }
                Item {
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 22
                    Label {
                        id: valueLabel
                        objectName: "rgbValue" + channelRow.index
                        anchors.fill: parent
                        visible: !channelRow.editing
                        text: String(channelRow.channel)
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        font.features: { "tnum": 1 }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.IBeamCursor
                            onClicked: channelRow.startEdit()
                            onWheel: wheel => {
                                channelRow.setChannel(channelRow.channel + (wheel.angleDelta.y > 0 ? 1 : -1))
                            }
                        }
                    }
                    TextField {
                        id: edit
                        objectName: "rgbEdit" + channelRow.index
                        anchors.fill: parent
                        visible: channelRow.editing
                        topPadding: 0; bottomPadding: 0; leftPadding: 0; rightPadding: 0
                        horizontalAlignment: TextInput.AlignRight
                        verticalAlignment: TextInput.AlignVCenter
                        font.features: { "tnum": 1 }
                        maximumLength: 3
                        inputMethodHints: Qt.ImhDigitsOnly
                        background: Rectangle {
                            color: "transparent"
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Material.accent }
                        }
                        function step(delta) {
                            const v = Math.max(0, Math.min(255, (parseInt(text) || 0) + delta))
                            text = String(v)
                            channelRow.setChannel(v)
                            selectAll()
                        }
                        Keys.onUpPressed: event => step(event.modifiers & Qt.ShiftModifier ? 10 : 1)
                        Keys.onDownPressed: event => step(event.modifiers & Qt.ShiftModifier ? -10 : -1)
                        // Esc is an application shortcut (back to base): claim it while typing.
                        Keys.onShortcutOverride: event => { if (event.key === Qt.Key_Escape) event.accepted = true }
                        Keys.onEscapePressed: channelRow.endEdit(false)
                        onAccepted: channelRow.endEdit(true)
                        onActiveFocusChanged: if (!activeFocus) channelRow.endEdit(true)
                        WheelHandler {
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            onWheel: event => edit.step(event.angleDelta.y > 0 ? 1 : -1)
                        }
                    }
                }
            }
        }
    }

    // Saved colors: click to use, right-click (or click an empty slot) to store the current one.
    Flow {
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: backend.savedColors
            delegate: Rectangle {
                required property int index
                required property string modelData
                width: 24; height: 24
                radius: Math.min(4, theme.radius)
                color: modelData === "" ? "transparent" : modelData
                border.color: Qt.rgba(1, 1, 1, modelData === "" ? 0.3 : 0.15)
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    visible: parent.modelData === ""
                    text: "+"
                    color: Qt.rgba(1, 1, 1, 0.5)
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton || parent.modelData === "")
                            backend.setSavedColor(parent.index, picker.color)
                        else
                            picker.pick(parent.modelData)
                    }
                }
                ToolTip.text: qsTr("Click to use, right-click to store the current color")
                ToolTip.visible: hover.hovered
                HoverHandler { id: hover }
            }
        }
    }
}
