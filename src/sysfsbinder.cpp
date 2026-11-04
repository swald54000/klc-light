#include "sysfsbinder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

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
        if (readTrimmed(path + "/idVendor") == "1038" && readTrimmed(path + "/idProduct") == "113a")
            return path;
    }
    // Device maps this wording (with the USB id) to "keyboard absent", exit status 2.
    *error = "SteelSeries KLC (1038:113a) not on the USB bus";
    return {};
}

bool SysfsBinder::writeAuthorized(const QString &path, const QByteArray &value, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(value) != value.size()) {
        *error = file.error() == QFileDevice::PermissionsError || !QFileInfo(path).isWritable()
                     ? QString("Add your user to the klc-light group (sudo usermod -aG klc-light $USER), then log in again.")
                     : QString("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
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
