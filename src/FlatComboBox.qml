import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A ComboBox whose list has a plain opaque background. Material's elevation shadow
// (RoundedElevationEffect) renders the list's background invisible with Qt 6.12, which
// left the choices floating over whatever was behind them.
ComboBox {
    popup.background: Rectangle {
        color: Material.dialogColor
        radius: Math.min(4, theme.radius)
        border.color: Qt.rgba(1, 1, 1, 0.12)
    }
}
