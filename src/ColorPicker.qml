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

    function pick(c) {
        picker.color = c
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

    RowLayout {
        spacing: 6
        Repeater {
            model: [qsTr("R"), qsTr("G"), qsTr("B")]
            delegate: RowLayout {
                required property int index
                required property string modelData
                spacing: 2
                Label { text: modelData; opacity: 0.7 }
                TextField {
                    id: rgbField
                    objectName: "rgbField" + index
                    // Wide enough for "255" whatever the style's padding (Material's left 14 px).
                    TextMetrics { id: widest; font: rgbField.font; text: "255" }
                    Layout.preferredWidth: Math.ceil(widest.advanceWidth) + leftPadding + rightPadding + 2
                    horizontalAlignment: TextInput.AlignHCenter
                    validator: IntValidator { bottom: 0; top: 255 }
                    text: Math.round([picker.color.r, picker.color.g, picker.color.b][index] * 255)
                    onEditingFinished: {
                        const c = [picker.color.r, picker.color.g, picker.color.b]
                        c[index] = Math.min(255, parseInt(text) || 0) / 255
                        picker.pick(Qt.rgba(c[0], c[1], c[2], 1))
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
