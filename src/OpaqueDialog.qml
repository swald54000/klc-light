import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A modal Dialog on a plain opaque background. Material's elevation effect hides the
// default one (same reason as FlatComboBox).
Dialog {
    modal: true
    background: Rectangle {
        color: Material.dialogColor
        radius: Math.min(4, theme.radius)
        border.color: Qt.rgba(1, 1, 1, 0.12)
    }
}
