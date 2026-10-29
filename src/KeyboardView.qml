import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The GS76 keyboard, drawn from the layout and animated by the preview. Click selects a
// key (Ctrl or Shift adds), dragging draws a selection rectangle.
Item {
    id: view
    implicitWidth: 0
    implicitHeight: 0

    // Milliseconds since the editor opened; drives the preview.
    property real clock: 0
    readonly property var keys: backend.keys
    // GG layout units: keys sit about 35 apart, x grows rightwards, y upwards.
    readonly property real unit: 35
    readonly property real minX: Math.min.apply(null, keys.map(k => k.left))
    readonly property real maxX: Math.max.apply(null, keys.map(k => k.right))
    readonly property real minY: Math.min.apply(null, keys.map(k => k.y)) - unit / 2
    readonly property real maxY: Math.max.apply(null, keys.map(k => k.y)) + unit / 2
    readonly property real scale: Math.min(width / (maxX - minX), height / (maxY - minY))
    readonly property real offsetX: (width - (maxX - minX) * scale) / 2
    readonly property real offsetY: (height - (maxY - minY) * scale) / 2

    function keyRect(k) {
        const h = unit * 0.88 * scale
        return Qt.rect(offsetX + (k.left - minX) * scale, offsetY + (maxY - k.y) * scale - h / 2,
                       (k.right - k.left) * scale, h)
    }

    function keysIn(r) {
        return keys.filter(k => {
            const kr = keyRect(k)
            return kr.x < r.x + r.width && kr.x + kr.width > r.x && kr.y < r.y + r.height && kr.y + kr.height > r.y
        }).map(k => k.id)
    }

    Timer {
        interval: 33
        running: view.visible
        repeat: true
        onTriggered: view.clock += interval
    }

    Repeater {
        model: view.keys
        delegate: Rectangle {
            required property var modelData
            readonly property rect r: view.keyRect(modelData)
            readonly property bool selected: backend.selection.indexOf(modelData.id) >= 0
            x: r.x
            y: r.y
            width: r.width
            height: r.height
            radius: Math.min(4, theme.radius)
            // Reading revision re-evaluates the color after every edit.
            color: backend.revision >= 0 ? backend.previewColor(modelData.id, view.clock) : "black"
            border.width: selected ? 2 : 1
            border.color: selected ? "white" : Qt.rgba(1, 1, 1, 0.12)

            Text {
                anchors.centerIn: parent
                text: parent.modelData.label
                font.pixelSize: Math.max(8, parent.height * 0.32)
                // Black or white, whichever reads on the key's current color.
                color: (0.299 * parent.color.r + 0.587 * parent.color.g + 0.114 * parent.color.b) > 0.55 ? "black" : "white"
            }
        }
    }

    Rectangle {
        id: band
        visible: false
        color: Qt.rgba(1, 1, 1, 0.08)
        border.color: Material.accent
    }

    MouseArea {
        anchors.fill: parent
        property point start
        property bool dragging: false

        onPressed: mouse => {
            start = Qt.point(mouse.x, mouse.y)
            dragging = false
        }
        onPositionChanged: mouse => {
            const r = Qt.rect(Math.min(start.x, mouse.x), Math.min(start.y, mouse.y),
                              Math.abs(mouse.x - start.x), Math.abs(mouse.y - start.y))
            if (!dragging && r.width + r.height < 6)
                return
            dragging = true
            band.x = r.x; band.y = r.y; band.width = r.width; band.height = r.height
            band.visible = true
        }
        onReleased: mouse => {
            const add = (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0
            if (dragging) {
                band.visible = false
                backend.select(view.keysIn(Qt.rect(band.x, band.y, band.width, band.height)), add)
                return
            }
            const hit = view.keysIn(Qt.rect(mouse.x, mouse.y, 1, 1))
            if (hit.length > 0)
                backend.select(hit, add)
            else if (!add)
                backend.clearSelection()
        }
    }
}
