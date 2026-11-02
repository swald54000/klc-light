import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Left column (it scrolls in a very short window): the base, the key groups, the animated-slot count and the
// reactive lighting.
ColumnLayout {
    id: column
    spacing: 3

    readonly property var groupList: backend.groups

    function typeName(type) {
        return ({ "static": qsTr("static"), "breathing": qsTr("breathing"), "flashing": qsTr("flashing"),
                  "rainbow": qsTr("rainbow"), "wave": qsTr("wave"), "pulse": qsTr("pulse") })[type] || type
    }

    ItemDelegate {
        id: baseRow
        objectName: "baseRow"
        Layout.fillWidth: true
        implicitHeight: 30
        topPadding: 0
        bottomPadding: 0
        highlighted: backend.currentKind === "base"
        // The text is what the row says, the contentItem below draws it.
        text: qsTr("BASE") + " " + column.typeName(backend.baseSummary.type)
        onClicked: backend.openBase()
        contentItem: RowLayout {
            spacing: 8
            Label { text: qsTr("BASE"); font.pixelSize: 11; opacity: 0.6 }
            Label { Layout.fillWidth: true; text: column.typeName(backend.baseSummary.type) }
            Rectangle {
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                radius: 7
                visible: backend.baseSummary.type !== "rainbow"
                color: backend.baseSummary.color
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.3)
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("GROUPS"); font.pixelSize: 11; opacity: 0.6 }
        Item { Layout.fillWidth: true }
        Label {
            objectName: "animatedCount"
            font.pixelSize: 11
            opacity: 0.8
            text: qsTr("Animated: %1 / 6").arg(backend.animatedCount)
            HoverHandler { id: animatedHover }
            ToolTip.visible: animatedHover.hovered
            ToolTip.delay: 400
            ToolTip.text: qsTr("The controller has 6 animation slots. Each animated group uses one, and so does an animated base.")
        }
    }
    ListView {
        objectName: "groupList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 74
        clip: true
        spacing: 2
        model: backend.groups
        delegate: ItemDelegate {
            id: row
            implicitHeight: 36
            topPadding: 0
            bottomPadding: 0
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
        implicitHeight: 30
        topPadding: 0
        bottomPadding: 0
        enabled: backend.selection.length > 0 && backend.currentKind === "selection"
        onClicked: {
            const problem = backend.createGroup(backend.defaultGroupName())
            fromSelectionError.text = problem
        }
    }
    // Qt shows no tooltip on a disabled button: say why the one above is greyed.
    Label {
        Layout.fillWidth: true
        objectName: "selectKeysHint"
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

    ItemDelegate {
        objectName: "reactiveRow"
        Layout.fillWidth: true
        implicitHeight: 30
        topPadding: 0
        bottomPadding: 0
        highlighted: backend.currentKind === "reactive"
        text: qsTr("REACTIVE (experimental)")
        opacity: backend.experimentalReactive ? 1 : 0.6
        onClicked: backend.openReactive()
    }
}
