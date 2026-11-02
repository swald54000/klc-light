import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A ComboBox whose list has a plain opaque background. Material's elevation shadow
// (RoundedElevationEffect) renders the list's background invisible with Qt 6.12, which
// left the choices floating over whatever was behind them.
// Its choices are often names the user typed: they show as plain text, never rich text.
ComboBox {
    id: control

    popup.background: Rectangle {
        color: Material.dialogColor
        radius: Math.min(4, theme.radius)
        border.color: Qt.rgba(1, 1, 1, 0.12)
    }

    // Material's own delegate, with a plain-text label.
    delegate: MenuItem {
        id: choice
        required property var model
        required property var modelData
        required property int index

        width: ListView.view.width
        text: control.textRole ? model[control.textRole] : modelData
        Material.foreground: control.currentIndex === index ? ListView.view.contentItem.Material.accent : ListView.view.contentItem.Material.foreground
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
        contentItem: Label {
            text: choice.text
            textFormat: Text.PlainText
            font: choice.font
            color: choice.enabled ? choice.Material.foreground : choice.Material.hintTextColor
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }
}
