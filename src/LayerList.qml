import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Left column: the base, the profile's key groups and the reactive lighting.
// Interim list until the group column is redesigned.
ColumnLayout {
    spacing: 8

    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 2
        // Base, then each group, then reactive.
        model: [{ kind: "base" }].concat(backend.groups.map((g, i) => ({ kind: "group", index: i, group: g })))
                                  .concat([{ kind: "reactive" }])
        delegate: ItemDelegate {
            required property var modelData
            width: ListView.view.width
            highlighted: modelData.kind === backend.currentKind
                         && (modelData.kind !== "group" || modelData.index === backend.currentGroup)
            onClicked: {
                if (modelData.kind === "base")
                    backend.openBase()
                else if (modelData.kind === "group")
                    backend.openGroup(modelData.index)
                else
                    backend.openReactive()
            }
            text: {
                const m = modelData
                if (m.kind === "base")
                    return backend.currentKind === "base" ? qsTr("Base: %1").arg(typeName(backend.current.type)) : qsTr("Base")
                if (m.kind === "group")
                    return qsTr("%1 (%2 keys)").arg(m.group.name).arg(m.group.count)
                return qsTr("Reactive (experimental)")
            }
            opacity: modelData.kind === "reactive" && !backend.experimentalReactive ? 0.5 : 1
        }
    }

    Button {
        Layout.fillWidth: true
        text: qsTr("Group %1").arg("+")
        enabled: backend.selection.length > 0 && backend.currentKind === "selection"
        onClicked: backend.createGroup(backend.defaultGroupName())
    }
    // Qt shows no tooltip on a disabled button: say why the one above is greyed.
    Label {
        objectName: "addEffectHint"
        Layout.fillWidth: true
        visible: backend.selection.length === 0
        wrapMode: Text.WordWrap
        font.pixelSize: 11
        opacity: 0.7
        text: qsTr("Select keys first to add an effect.")
    }
    Button {
        Layout.fillWidth: true
        text: qsTr("Remove effect")
        enabled: backend.currentKind === "group"
        onClicked: backend.deleteGroup(backend.currentGroup)
    }

    function typeName(type) {
        return ({ "static": qsTr("static"), "breathing": qsTr("breathing"), "flashing": qsTr("flashing"),
                  "rainbow": qsTr("rainbow"), "wave": qsTr("wave") })[type] || type
    }
}
