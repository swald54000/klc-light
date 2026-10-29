import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Left column: the active theme's profile and its layers.
ColumnLayout {
    spacing: 8

    Label { text: qsTr("PROFILE"); font.pixelSize: 11; opacity: 0.6 }
    Label {
        Layout.fillWidth: true
        text: qsTr("Theme: %1").arg(backend.themeName === "" ? qsTr("none") : backend.themeName)
        font.bold: true
        elide: Text.ElideRight
    }
    Label {
        Layout.fillWidth: true
        text: backend.profilePath.replace(/^.*\/themes\//, "themes/")
        font.pixelSize: 10
        opacity: 0.5
        elide: Text.ElideMiddle
    }

    Label { text: qsTr("LAYERS"); font.pixelSize: 11; opacity: 0.6; Layout.topMargin: 8 }

    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 2
        model: backend.layers
        delegate: ItemDelegate {
            required property int index
            required property var modelData
            width: ListView.view.width
            highlighted: index === backend.currentLayer
            onClicked: backend.currentLayer = index
            text: {
                const m = modelData
                if (m.kind === "base")
                    return qsTr("Base: %1").arg(typeName(m.type))
                if (m.kind === "keys")
                    return qsTr("Keys (%1)").arg(m.count)
                if (m.kind === "effect")
                    return qsTr("%1 (%2 keys)").arg(m.name).arg(m.count)
                return qsTr("Reactive (experimental)")
            }
            opacity: modelData.kind === "reactive" && !backend.experimentalReactive ? 0.5 : 1
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Button {
            Layout.fillWidth: true
            text: qsTr("+ Wave")
            enabled: backend.selection.length > 0
            onClicked: backend.addEffect("wave")
            ToolTip.text: qsTr("New wave effect on the selected keys")
            ToolTip.visible: hovered
        }
        Button {
            Layout.fillWidth: true
            text: qsTr("+ Pulse")
            enabled: backend.selection.length > 0
            onClicked: backend.addEffect("pulse")
            ToolTip.text: qsTr("New pulse effect on the selected keys")
            ToolTip.visible: hovered
        }
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Remove effect")
        enabled: backend.currentKind === "effect"
        onClicked: backend.removeLayer(backend.currentLayer)
    }

    function typeName(type) {
        return ({ "static": qsTr("static"), "breathing": qsTr("breathing"), "flashing": qsTr("flashing"),
                  "rainbow": qsTr("rainbow"), "wave": qsTr("wave") })[type] || type
    }
}
