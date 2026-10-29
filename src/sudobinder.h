#pragma once

#include "device.h"

// Runs the msi-kbd-usb helper through passwordless sudo; never prompts.
class SudoBinder : public Binder {
public:
    explicit SudoBinder(QString helper = "/usr/local/bin/msi-kbd-usb");
    bool bind(QString *error) override;
    bool unbind(QString *error) override;

private:
    bool run(const QString &action, QString *error);
    QString m_helper;
};
