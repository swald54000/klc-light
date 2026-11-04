#include "sysfsbinder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>

namespace {

QByteArray readTrimmed(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
}

} // namespace

SysfsBinder::SysfsBinder(QString devicesDir) : m_devicesDir(std::move(devicesDir)) {}

QString SysfsBinder::device(QString *error) const {
    const QDir dir(m_devicesDir);
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = dir.filePath(name);
        // The controller is built in ("fixed"): a removable device claiming its id is never
        // touched, so authorising it cannot get around USBGuard's default policy.
        if (readTrimmed(path + "/idVendor") == "1038" && readTrimmed(path + "/idProduct") == "113a"
            && readTrimmed(path + "/removable") == "fixed")
            return path;
    }
    // Device maps this wording (with the USB id) to "keyboard absent", exit status 2.
    *error = "SteelSeries KLC (1038:113a) not on the USB bus";
    return {};
}

QString SysfsBinder::fileGroup(const QString &path) const {
    return QFileInfo(path).group();
}

bool SysfsBinder::writeAuthorized(const QString &path, const QByteArray &value, QString *error) {
    QFile file(path);
    // Unbuffered, so the kernel's answer to write(2) is the one checked here.
    errno = 0;
    if (file.open(QIODevice::WriteOnly | QIODevice::Unbuffered) && file.write(value) == value.size())
        return true;
    const bool denied = errno == EACCES || errno == EPERM;
    if (denied && fileGroup(path) == "klc-light")
        *error = "Add your user to the klc-light group (sudo usermod -aG klc-light $USER), then log in again.";
    else if (denied)
        *error = "The klc-light udev rule has not run on the controller yet: restart the computer "
                 "(or run sudo udevadm trigger --action=add --attr-match=idVendor=1038).";
    else
        *error = QString("Cannot write %1: %2").arg(path, file.errorString());
    return false;
}

bool SysfsBinder::bind(QString *error) {
    const QString path = device(error);
    if (path.isEmpty())
        return false;
    if (!writeAuthorized(path + "/authorized", "0", error) || !writeAuthorized(path + "/authorized", "1", error))
        return false;
    // Detached from its driver (by the old msi-kbd-usb helper), the controller is authorised
    // but never configured again; the kernel binds it at the next boot.
    if (!QFileInfo::exists(path + "/driver")) {
        *error = "The lighting controller lost its USB driver: shut the computer down completely (power off), then start it again.";
        return false;
    }
    return true;
}

bool SysfsBinder::unbind(QString *error) {
    const QString path = device(error);
    return !path.isEmpty() && writeAuthorized(path + "/authorized", "0", error);
}
