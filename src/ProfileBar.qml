import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One compact row above the keyboard: the open profile (a picker), New / Duplicate / Rename /
// Delete, and the theme the profile is linked to. Switching asks first when there are
// unsaved changes.
RowLayout {
    id: bar
    spacing: 6

    // The profile the user asked for while the switch dialog is open.
    property string pendingProfile: ""
    // Which operation the name dialog is for: "new", "duplicate" or "rename".
    property string nameMode: "new"

    function syncProfile() {
        const names = backend.profiles
        profileBox.currentIndex = names.indexOf(backend.profileName)
    }
    function syncTheme() {
        themeBox.currentIndex = Math.max(0, backend.themes.indexOf(backend.linkedTheme) + 1)
    }
    function openName(mode) {
        nameMode = mode
        profileNameField.text = mode === "rename" ? backend.profileName
                              : mode === "duplicate" ? qsTr("%1 (copy)").arg(backend.profileName) : ""
        profileNameDialog.open()
        profileNameField.forceActiveFocus()
    }
    function switchTo(name) {
        if (name === backend.profileName)
            return
        if (backend.dirty) {
            pendingProfile = name
            switchDialog.open()
        } else {
            backend.openProfile(name)
        }
    }

    Connections {
        target: backend
        function onLibraryChanged() { bar.syncProfile(); bar.syncTheme() }
        function onProfileChanged() { bar.syncProfile(); bar.syncTheme() }
    }
    Component.onCompleted: { syncProfile(); syncTheme() }

    FlatComboBox {
        id: profileBox
        objectName: "profileBox"
        Layout.preferredWidth: 200
        Layout.preferredHeight: 32
        enabled: !backend.busy
        textRole: "text"
        valueRole: "name"
        // Marked: the applied profile, and those that cannot be read (opening one says why).
        model: backend.profiles.map(n => ({
            name: n,
            text: n + (n === backend.appliedName ? " · " + qsTr("applied") : "")
                    + (backend.unreadableProfiles.indexOf(n) >= 0 ? " · " + qsTr("unreadable") : "")
        }))
        onActivated: index => {
            const name = model[index].name
            bar.switchTo(name)
            bar.syncProfile() // shows the open profile again until the switch really happens
        }
    }

    Repeater {
        model: [
            { name: "newProfileButton", text: qsTr("New"), mode: "new" },
            { name: "duplicateProfileButton", text: qsTr("Duplicate"), mode: "duplicate" },
            { name: "renameProfileButton", text: qsTr("Rename"), mode: "rename" },
            { name: "deleteProfileButton", text: qsTr("Delete"), mode: "delete" }
        ]
        delegate: Button {
            required property var modelData
            objectName: modelData.name
            text: modelData.text
            flat: true
            enabled: !backend.busy
            Layout.preferredHeight: 32
            leftPadding: 8
            rightPadding: 8
            onClicked: modelData.mode === "delete" ? deleteProfileDialog.open() : bar.openName(modelData.mode)
        }
    }

    Item { Layout.fillWidth: true }

    Label {
        id: themeNote
        objectName: "themeNote"
        visible: text !== ""
        font.pixelSize: 11
        color: "#e0c060"
        elide: Text.ElideRight
        Layout.maximumWidth: 220
        Timer { id: noteTimer; interval: 5000; onTriggered: themeNote.text = "" }
    }

    Label { text: qsTr("Linked theme:"); font.pixelSize: 11; opacity: 0.6 }

    FlatComboBox {
        id: themeBox
        objectName: "themeBox"
        Layout.preferredWidth: 160
        Layout.preferredHeight: 32
        textRole: "text"
        model: [{ value: "", text: qsTr("none") }].concat(backend.themes.map(t => ({ value: t, text: t })))
        onActivated: index => {
            const previous = backend.linkTheme(model[index].value)
            themeNote.text = previous !== "" ? qsTr("was linked to %1").arg(previous) : ""
            if (previous !== "")
                noteTimer.restart()
            bar.syncTheme()
        }
    }

    OpaqueDialog {
        id: switchDialog
        objectName: "switchDialog"
        width: 420
        anchors.centerIn: Overlay.overlay
        title: qsTr("Unsaved changes")
        footer: DialogButtonBox {
            Button {
                objectName: "switchSave"
                text: qsTr("Save")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                objectName: "switchDiscard"
                text: qsTr("Discard")
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            }
            Button {
                objectName: "switchCancel"
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Save the changes to “%1” before opening “%2”?").arg(backend.profileName).arg(bar.pendingProfile)
        }
        onAccepted: { if (backend.save()) backend.openProfile(bar.pendingProfile) }
        onDiscarded: { switchDialog.close(); backend.openProfile(bar.pendingProfile) }
    }

    OpaqueDialog {
        id: profileNameDialog
        objectName: "profileNameDialog"
        width: 360
        anchors.centerIn: Overlay.overlay
        title: bar.nameMode === "new" ? qsTr("New profile")
             : bar.nameMode === "duplicate" ? qsTr("Duplicate profile") : qsTr("Rename profile")
        // Renaming may keep the current name, or change only its case.
        readonly property string problem: backend.nameError(profileNameField.text, bar.nameMode === "rename" ? backend.profileName : "")
        readonly property bool valid: profileNameField.text.trim() !== "" && problem === ""
        function confirm() {
            if (!valid)
                return
            const name = profileNameField.text
            const error = bar.nameMode === "new" ? backend.newProfile(name)
                        : bar.nameMode === "duplicate" ? backend.duplicateProfile(name) : backend.renameProfile(name)
            if (error === "")
                close()
        }
        ColumnLayout {
            spacing: 6
            TextField {
                id: profileNameField
                objectName: "profileNameField"
                Layout.fillWidth: true
                placeholderText: qsTr("Name")
                onAccepted: profileNameDialog.confirm()
            }
            Label {
                objectName: "profileNameError"
                Layout.fillWidth: true
                visible: profileNameDialog.problem !== ""
                text: profileNameDialog.problem
                color: "#ff8a80"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "profileNameOk"
                text: qsTr("OK")
                enabled: profileNameDialog.valid
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: profileNameDialog.confirm()
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: profileNameDialog.close()
            }
        }
    }

    OpaqueDialog {
        id: deleteProfileDialog
        objectName: "deleteProfileDialog"
        width: 360
        anchors.centerIn: Overlay.overlay
        title: qsTr("Delete this profile?")
        footer: DialogButtonBox {
            Button {
                text: qsTr("Delete")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("“%1” will be deleted.").arg(backend.profileName)
                  + (backend.linkedTheme !== "" ? " " + qsTr("The theme %1 loses its link.").arg(backend.linkedTheme) : "")
        }
        onAccepted: backend.deleteProfile()
    }
}
