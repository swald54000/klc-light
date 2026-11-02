import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The GS76 keyboard, drawn to the key rectangles measured on a photo of it and lit like
// the real one: dark keycaps, the light showing through the legends and around the edges.
// Click selects a key (Ctrl or Shift adds), dragging draws a selection rectangle.
Item {
    id: view
    implicitWidth: 0
    implicitHeight: 0

    // Milliseconds since the editor opened; drives the preview.
    property real clock: 0
    readonly property var keys: backend.keys
    // Layout pixels, as measured on the photo, plus a margin of chassis around the keys.
    readonly property real margin: 14
    readonly property real minX: Math.min.apply(null, keys.map(k => k.left)) - margin
    readonly property real maxX: Math.max.apply(null, keys.map(k => k.right)) + margin
    readonly property real minY: Math.min.apply(null, keys.map(k => k.top)) - margin
    readonly property real maxY: Math.max.apply(null, keys.map(k => k.bottom)) + margin
    readonly property real scale: Math.min(width / (maxX - minX), height / (maxY - minY))
    readonly property real offsetX: (width - (maxX - minX) * scale) / 2
    readonly property real offsetY: (height - (maxY - minY) * scale) / 2

    function keyRect(k) {
        return Qt.rect(offsetX + (k.left - minX) * scale, offsetY + (k.top - minY) * scale,
                       (k.right - k.left) * scale, (k.bottom - k.top) * scale)
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

    // The laptop deck around the keys.
    Rectangle {
        x: view.offsetX
        y: view.offsetY
        width: (view.maxX - view.minX) * view.scale
        height: (view.maxY - view.minY) * view.scale
        radius: 10 * view.scale
        gradient: Gradient {
            GradientStop { position: 0; color: "#1b1c20" }
            GradientStop { position: 1; color: "#111215" }
        }
        border.color: "#2a2b31"
    }

    Repeater {
        model: view.keys
        delegate: Item {
            id: key
            required property var modelData
            readonly property rect r: view.keyRect(modelData)
            readonly property bool selected: backend.selection.indexOf(modelData.id) >= 0
            // Key of the open group: a lighter veil and a thin outline, apart from the selection.
            readonly property bool inGroup: backend.groupKeys.indexOf(modelData.id) >= 0
            // Reading revision re-evaluates the color after every edit.
            readonly property color light: backend.revision >= 0 ? backend.previewColor(modelData.id, view.clock) : "black"
            // How bright the LED is, to fade the glow out for dark or switched-off keys.
            readonly property real level: Math.max(light.r, light.g, light.b)
            x: r.x
            y: r.y
            width: r.width
            height: r.height

            // Light leaking from under the keycap.
            Rectangle {
                anchors.fill: parent
                anchors.margins: -Math.max(1.5, 3 * view.scale)
                radius: 6 * view.scale
                color: key.light
                opacity: 0.55 * key.level
            }

            // The keycap.
            Rectangle {
                id: cap
                anchors.fill: parent
                radius: 4 * view.scale
                gradient: Gradient {
                    GradientStop { position: 0; color: "#2a2b30" }
                    GradientStop { position: 1; color: "#1d1e22" }
                }
                border.width: key.selected ? Math.max(2, 2.5 * view.scale) : 1
                border.color: key.selected ? "white" : Qt.rgba(0, 0, 0, 0.6)
            }

            // The open group's keys: accent-tinted veil with a thin accent outline.
            Rectangle {
                anchors.fill: parent
                radius: cap.radius
                visible: key.inGroup
                color: Qt.rgba(1, 1, 1, 0.07)
                border.width: 1
                border.color: Qt.rgba(Material.accent.r, Material.accent.g, Material.accent.b, 0.8)
            }

            // A light veil so the selection also stands out on brightly lit keys.
            Rectangle {
                anchors.fill: parent
                radius: cap.radius
                visible: key.selected
                color: Qt.rgba(1, 1, 1, 0.14)
            }

            // The legend, lit from below in the key's color.
            Text {
                anchors.centerIn: parent
                width: parent.width - 4
                height: parent.height * 0.6
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                // Shrinks long legends (PrtSc, ScrLk) to fit their key instead of cutting them.
                fontSizeMode: Text.Fit
                minimumPixelSize: 5
                text: key.modelData.label
                font.pixelSize: Math.max(7, parent.height * 0.42)
                font.bold: true
                font.letterSpacing: 0.5
                // A switched-off LED leaves a faint grey legend, as on the real keyboard.
                color: key.level < 0.08 ? "#4a4b52" : Qt.lighter(key.light, 1.15)
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
