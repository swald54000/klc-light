#include "sudobinder.h"

#include <QFileInfo>
#include <QProcess>

SudoBinder::SudoBinder(QString helper) : m_helper(std::move(helper)) {}

bool SudoBinder::bind(QString *error) { return run("bind", error); }
bool SudoBinder::unbind(QString *error) { return run("unbind", error); }

bool SudoBinder::run(const QString &action, QString *error) {
    if (!QFileInfo(m_helper).isExecutable()) {
        *error = QString("%1 not found; install it from ~/.local/share/msi-perkeyrgb-setup").arg(m_helper);
        return false;
    }
    QProcess process;
    process.setStandardInputFile(QProcess::nullDevice());
    process.start("sudo", {"-n", m_helper, action});
    if (!process.waitForFinished(2000)) {
        process.kill();
        process.waitForFinished(500);
        *error = QString("%1 %2 timed out").arg(m_helper, action);
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error->isEmpty())
            *error = QString("%1 %2 failed").arg(m_helper, action);
        return false;
    }
    return true;
}
