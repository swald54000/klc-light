#include "keylabels.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>

namespace {

// Short labels for keys that print nothing.
QString symbolLabel(xkb_keysym_t sym) {
    static const QHash<xkb_keysym_t, QString> labels = {
        {XKB_KEY_Escape, "Esc"}, {XKB_KEY_Return, "↵"}, {XKB_KEY_KP_Enter, "↵"},
        {XKB_KEY_BackSpace, "⌫"}, {XKB_KEY_Tab, "Tab"}, {XKB_KEY_ISO_Left_Tab, "Tab"},
        {XKB_KEY_Caps_Lock, "Caps"}, {XKB_KEY_Shift_L, "Shift"}, {XKB_KEY_Shift_R, "Shift"},
        {XKB_KEY_Control_L, "Ctrl"}, {XKB_KEY_Control_R, "Ctrl"}, {XKB_KEY_Alt_L, "Alt"},
        {XKB_KEY_Alt_R, "Alt"}, {XKB_KEY_ISO_Level3_Shift, "AltGr"}, {XKB_KEY_Super_L, "❖"},
        {XKB_KEY_Super_R, "❖"}, {XKB_KEY_space, ""}, {XKB_KEY_Up, "↑"}, {XKB_KEY_Down, "↓"},
        {XKB_KEY_Left, "←"}, {XKB_KEY_Right, "→"}, {XKB_KEY_Delete, "Del"}, {XKB_KEY_Insert, "Ins"},
        {XKB_KEY_Home, "Home"}, {XKB_KEY_End, "End"}, {XKB_KEY_Prior, "PgUp"}, {XKB_KEY_Next, "PgDn"},
        {XKB_KEY_Print, "PrtSc"}, {XKB_KEY_Num_Lock, "Num"}, {XKB_KEY_Menu, "Menu"},
        {XKB_KEY_Scroll_Lock, "ScrLk"}, {XKB_KEY_Pause, "Pause"}, {XKB_KEY_XF86Fn, "Fn"},
        {XKB_KEY_XF86PowerOff, "⏻"},
        // Dead keys print nothing on their own; show the accent they add.
        {XKB_KEY_dead_circumflex, "^"}, {XKB_KEY_dead_diaeresis, "¨"}, {XKB_KEY_dead_grave, "`"},
        {XKB_KEY_dead_acute, "´"}, {XKB_KEY_dead_tilde, "~"},
    };
    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12)
        return QString("F%1").arg(sym - XKB_KEY_F1 + 1);
    return labels.value(sym);
}

int evdevCode(const QString &id) {
    // The ids are the kernel's names; only the subset the GS76 has needs a lookup, and
    // xkbcommon resolves evdev codes, so map names through the kernel numbering.
    static const QHash<QString, int> codes = [] {
        QHash<QString, int> map;
#define K(name) map.insert(#name, name)
        K(KEY_ESC); K(KEY_1); K(KEY_2); K(KEY_3); K(KEY_4); K(KEY_5); K(KEY_6); K(KEY_7); K(KEY_8);
        K(KEY_9); K(KEY_0); K(KEY_MINUS); K(KEY_EQUAL); K(KEY_BACKSPACE); K(KEY_TAB); K(KEY_Q);
        K(KEY_W); K(KEY_E); K(KEY_R); K(KEY_T); K(KEY_Y); K(KEY_U); K(KEY_I); K(KEY_O); K(KEY_P);
        K(KEY_LEFTBRACE); K(KEY_RIGHTBRACE); K(KEY_ENTER); K(KEY_LEFTCTRL); K(KEY_A); K(KEY_S);
        K(KEY_D); K(KEY_F); K(KEY_G); K(KEY_H); K(KEY_J); K(KEY_K); K(KEY_L); K(KEY_SEMICOLON);
        K(KEY_APOSTROPHE); K(KEY_GRAVE); K(KEY_LEFTSHIFT); K(KEY_BACKSLASH); K(KEY_Z); K(KEY_X);
        K(KEY_C); K(KEY_V); K(KEY_B); K(KEY_N); K(KEY_M); K(KEY_COMMA); K(KEY_DOT); K(KEY_SLASH);
        K(KEY_RIGHTSHIFT); K(KEY_KPASTERISK); K(KEY_LEFTALT); K(KEY_SPACE); K(KEY_CAPSLOCK);
        K(KEY_F1); K(KEY_F2); K(KEY_F3); K(KEY_F4); K(KEY_F5); K(KEY_F6); K(KEY_F7); K(KEY_F8);
        K(KEY_F9); K(KEY_F10); K(KEY_F11); K(KEY_F12); K(KEY_NUMLOCK); K(KEY_SCROLLLOCK);
        K(KEY_KP7); K(KEY_KP8); K(KEY_KP9); K(KEY_KPMINUS); K(KEY_KP4); K(KEY_KP5); K(KEY_KP6);
        K(KEY_KPPLUS); K(KEY_KP1); K(KEY_KP2); K(KEY_KP3); K(KEY_KP0); K(KEY_KPDOT); K(KEY_102ND);
        K(KEY_KPENTER); K(KEY_RIGHTCTRL); K(KEY_KPSLASH); K(KEY_SYSRQ); K(KEY_RIGHTALT);
        K(KEY_HOME); K(KEY_UP); K(KEY_PAGEUP); K(KEY_LEFT); K(KEY_RIGHT); K(KEY_END); K(KEY_DOWN);
        K(KEY_PAGEDOWN); K(KEY_INSERT); K(KEY_DELETE); K(KEY_PAUSE); K(KEY_LEFTMETA);
        K(KEY_RIGHTMETA); K(KEY_COMPOSE); K(KEY_POWER); K(KEY_FN);
#undef K
        return map;
    }();
    return codes.value(id, -1);
}

} // namespace

KeyLabels KeyLabels::fromRmlvo(const QString &layout, const QString &variant) {
    KeyLabels result;
    xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context)
        return result;
    const QByteArray layoutBytes = layout.toUtf8(), variantBytes = variant.toUtf8();
    const xkb_rule_names names{nullptr, nullptr, layoutBytes.constData(), variantBytes.constData(), nullptr};
    xkb_keymap *keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    xkb_state *state = keymap ? xkb_state_new(keymap) : nullptr;
    if (state) {
        for (const Key &key : Layout::gs76().keys()) {
            const int code = evdevCode(key.id);
            if (code < 0)
                continue;
            // xkbcommon numbers keys as evdev code + 8, the X11 convention.
            const xkb_keycode_t keycode = xkb_keycode_t(code + 8);
            xkb_keysym_t sym = xkb_state_key_get_one_sym(state, keycode);
            // The keypad shows its Num Lock level (digits), as printed on the keys.
            const xkb_keysym_t *levelSyms = nullptr;
            if (sym >= XKB_KEY_KP_Space && sym <= XKB_KEY_KP_9
                && xkb_keymap_key_get_syms_by_level(keymap, keycode, 0, 1, &levelSyms) > 0)
                sym = levelSyms[0];
            QString label = symbolLabel(sym);
            if (label.isNull()) {
                char utf8[16] = {};
                xkb_keysym_to_utf8(sym, utf8, sizeof utf8);
                label = QString::fromUtf8(utf8).toUpper();
            }
            if (label.isNull() || (label.isEmpty() && sym != XKB_KEY_space)) {
                char name[64] = {};
                xkb_keysym_get_name(sym, name, sizeof name);
                label = QString::fromLatin1(name);
            }
            result.m_labels.insert(key.id, label);
        }
    }
    xkb_state_unref(state);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    return result;
}

KeyLabels KeyLabels::current() {
    auto option = [](const char *name) {
        QProcess process;
        process.start("hyprctl", {"-j", "getoption", name});
        if (!process.waitForFinished(1000))
            return QString();
        return QJsonDocument::fromJson(process.readAllStandardOutput()).object().value("str").toString().trimmed();
    };
    // Hyprland lists layouts comma-separated; the first is the one shown by default.
    QString layout = option("input:kb_layout").section(',', 0, 0).trimmed();
    QString variant = option("input:kb_variant").section(',', 0, 0).trimmed();
    if (layout.isEmpty() || layout == "[[EMPTY]]")
        layout = "us";
    if (variant == "[[EMPTY]]")
        variant.clear();
    return fromRmlvo(layout, variant);
}

QString KeyLabels::label(const Key &key) const {
    return m_labels.value(key.id, key.id.mid(4));
}
