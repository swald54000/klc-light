#include "layout.h"

#include "layoutdata.h"

const Layout &Layout::gs76() {
    static const Layout layout;
    return layout;
}

Layout::Layout() {
    for (int i = 0; i < kKeyDataCount; ++i) {
        const KeyData &d = kKeyData[i];
        m_byId.insert(QString::fromLatin1(d.id), m_keys.size());
        m_byHid.insert(d.hid, m_keys.size());
        m_keys.append({QString::fromLatin1(d.id), d.hid, d.region, d.x, d.y, double(d.w), double(d.h), d.visible, d.cx, d.cy});
    }
}

QList<Key> Layout::keys() const {
    QList<Key> visible;
    for (const Key &key : m_keys)
        if (key.visible)
            visible.append(key);
    return visible;
}

const Key *Layout::byId(const QString &id) const {
    const auto it = m_byId.constFind(id);
    return it == m_byId.constEnd() ? nullptr : &m_keys[*it];
}

const Key *Layout::byHid(quint8 hid) const {
    const auto it = m_byHid.constFind(hid);
    return it == m_byHid.constEnd() ? nullptr : &m_keys[*it];
}

QList<quint8> Layout::regionHids(Region region) const {
    const int r = int(region);
    return QList<quint8>(kRegionHids[r], kRegionHids[r] + 42);
}

QList<quint8> Layout::aquaHids(Region region) const {
    return regionHids(region).mid(0, kAquaPrefix[int(region)]);
}

QString Layout::lightSource(const QString &id) const {
    return id == "KEY_POWER" ? QString("KEY_F1") : id;
}

QStringList Layout::group(const QString &name) const {
    if (name == "wasd")
        return {"KEY_W", "KEY_A", "KEY_S", "KEY_D"};
    if (name == "function") {
        QStringList ids;
        for (int i = 1; i <= 12; ++i)
            ids << QString("KEY_F%1").arg(i);
        return ids;
    }
    if (name == "digits")
        return {"KEY_1", "KEY_2", "KEY_3", "KEY_4", "KEY_5", "KEY_6", "KEY_7", "KEY_8", "KEY_9", "KEY_0"};
    if (name == "modifiers")
        return {"KEY_LEFTCTRL", "KEY_LEFTSHIFT", "KEY_LEFTALT", "KEY_LEFTMETA",
                "KEY_RIGHTCTRL", "KEY_RIGHTSHIFT", "KEY_RIGHTALT"};
    QStringList ids;
    for (const Key &key : keys())
        if (name == "all" || (name == "numpad" && (key.id.startsWith("KEY_KP") || key.id == "KEY_NUMLOCK")))
            ids << key.id;
    return ids;
}
