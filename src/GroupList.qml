import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Left column (it scrolls in a short window): the profile, the base, the key groups, the animated-slot count and the
// reactive lighting.
ColumnLayout {
    id: column
    spacing: 6

    readonly property var groupList: backend.groups

    function typeName(type) {
        return ({ "static": qsTr("static"), "breathing": qsTr("breathing"), "flashing": qsTr("flashing"),
                  "rainbow": qsTr("rainbow"), "wave": qsTr("wave"), "pulse": qsTr("pulse") })[type] || type
    }

    // The profile bar comes later: for now just the open profile's name.
    Label {
        Layout.fillWidth: true
        text: backend.profileName
        font.bold: true
        elide: Text.ElideRight
    }

    Label { text: qsTr("BASE"); font.pixelSize: 11; opacity: 0.6 }
    ItemDelegate {
        objectName: "baseRow"
        Layout.fillWidth: true
        highlighted: backend.currentKind === "base"
        text: backend.currentKind === "base" ? qsTr("Base: %1").arg(column.typeName(backend.current.type)) : qsTr("Base")
        onClicked: backend.openBase()
    }

    Label { text: qsTr("GROUPS"); font.pixelSize: 11; opacity: 0.6 }
    ListView {
        objectName: "groupList"
        Layout.fillWidth: true
        // The whole column scrolls (in Main), not the list.
        Layout.preferredHeight: contentHeight
        interactive: false
        spacing: 2
        model: backend.groups
        delegate: ItemDelegate {
            id: row
            required property var modelData
            required property int index
            objectName: "groupRow" + index
            width: ListView.view.width
            highlighted: backend.currentKind === "group" && backend.currentGroup === index
            onClicked: backend.openGroup(index)
            contentItem: RowLayout {
                spacing: 8
                Rectangle {
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    radius: 7
                    color: row.modelData.color
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.3)
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.name
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        font.pixelSize: 11
                        opacity: 0.6
                        elide: Text.ElideRight
                        text: qsTr("%1 · %n key(s)", "", row.modelData.count).arg(column.typeName(row.modelData.behavior))
                    }
                }
            }
        }
    }

    Button {
        objectName: "createFromSelectionButton"
        Layout.fillWidth: true
        text: qsTr("+ Group from selection")
        enabled: backend.selection.length > 0 && backend.currentKind === "selection"
        onClicked: {
            const problem = backend.createGroup(backend.defaultGroupName())
            fromSelectionError.text = problem
        }
    }
    // Qt shows no tooltip on a disabled button: say why the one above is greyed.
    Label {
        Layout.fillWidth: true
        visible: backend.selection.length === 0
        wrapMode: Text.WordWrap
        font.pixelSize: 11
        opacity: 0.7
        text: qsTr("Select keys first.")
    }
    Label {
        id: fromSelectionError
        objectName: "fromSelectionError"
        Layout.fillWidth: true
        visible: text !== ""
        wrapMode: Text.WordWrap
        font.pixelSize: 11
        color: "#ff8a80"
        Connections {
            target: backend
            function onSelectionChanged() { fromSelectionError.text = "" }
        }
    }

    Label {
        objectName: "animatedCount"
        Layout.fillWidth: true
        font.pixelSize: 11
        opacity: 0.8
        text: qsTr("Animated: %1 / 6").arg(backend.animatedCount)
        HoverHandler { id: animatedHover }
        ToolTip.visible: animatedHover.hovered
        ToolTip.delay: 400
        ToolTip.text: qsTr("The controller has 6 animation slots. Each animated group uses one, and so does an animated base.")
    }

    ItemDelegate {
        objectName: "reactiveRow"
        Layout.fillWidth: true
        highlighted: backend.currentKind === "reactive"
        text: qsTr("REACTIVE (experimental)")
        opacity: backend.experimentalReactive ? 1 : 0.6
        onClicked: backend.openReactive()
    }
}
