#pragma once

#include <QByteArray>
#include <QString>

#include "device.h"

// Attaches and detaches the KLC through its own sysfs `authorized` file: writing 0 then 1
// makes the kernel configure it again (its hidraw node reappears), writing 0 leaves it
// disconnected, the state the controller needs to show what was sent. The package's udev
// rule gives that one file to the klc-light group, so no root is involved.
class SysfsBinder : public Binder {
public:
    explicit SysfsBinder(QString devicesDir = "/sys/bus/usb/devices");
    bool bind(QString *error) override;
    bool unbind(QString *error) override;

protected:
    virtual bool writeAuthorized(const QString &path, const QByteArray &value, QString *error);
    // The group owning a file, to tell "not in the group" from "the udev rule has not run".
    virtual QString fileGroup(const QString &path) const;

private:
    QString device(QString *error) const; // the controller's sysfs directory, empty if absent
    QString m_devicesDir;
};
