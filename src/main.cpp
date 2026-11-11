// klc-light — edits the per-key lighting of the MSI GS76 keyboard, one profile per Omarchy theme.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
#include <QUrl>

#include <QTextStream>
#include <QStandardPaths>
#include <QThread>

#include "version.h"
#include "backend.h"
#include "cli.h"
#include "device.h"
#include "hidapitransport.h"
#include "sysfsbinder.h"
#include "theme.h"
#include "translator.h"
#include "visualizer.h"

namespace {

// `apply` runs from theme hooks with no window, so it needs no display.
int runCommand(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    // The first run's migration may name a profile; messages are read by the user too.
    Translator translator;
    app.installTranslator(&translator);

    HidapiTransport transport;
    SysfsBinder binder;
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty())
        runtime = QDir::tempPath();
    Device device(transport, binder, runtime, [](int ms) { QThread::msleep(ms); });

    QTextStream out(stdout), err(stderr);
    CliEnv env{ThemePaths::standard(), QDir::homePath() + "/.config/klc-light", &device, &out, &err,
               qEnvironmentVariableIntValue("KLC_LIGHT_FAKE_DEVICE") == 1};
    return runCli(args, env);
}

} // namespace

int main(int argc, char *argv[]) {
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    // The visualizer's service keeps an event loop; every other command runs and returns.
    if (command == "visualizer" && argc > 2 && QString::fromLocal8Bit(argv[2]) == "run")
        return runVisualizer(argc, argv);
    if (!command.isEmpty() && command != "open")
        return runCommand(argc, argv);

    QGuiApplication app(argc, argv);
    app.setApplicationName("klc-light");
    app.setApplicationVersion(kVersion);

    // Matches the window to klc-light.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("klc-light");
    app.setWindowIcon(QIcon::fromTheme("klc-light"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;

    // KLC_LIGHT_FAKE_DEVICE=1 prints the packets instead of sending them, for trying the
    // editor without touching the keyboard.
    HidapiTransport transport;
    SysfsBinder binder;
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty())
        runtime = QDir::tempPath();
    Device device(transport, binder, runtime, [](int ms) { QThread::msleep(ms); });
    const bool fake = qEnvironmentVariableIntValue("KLC_LIGHT_FAKE_DEVICE") == 1;
    Backend::Sender sender = [&device, fake](const QList<Packet> &packets) {
        if (!fake)
            return device.apply(packets);
        QTextStream err(stderr);
        for (const Packet &packet : packets)
            err << (packet.kind == Packet::Feature ? "F " : "O ") << packet.data.toHex() << "\n";
        return ApplyResult{};
    };
    Backend backend(ThemePaths::standard(), QDir::homePath() + "/.config/klc-light", KeyLabels::current(), sender,
                    QDir::homePath() + "/.config/klc-light/colors.json");

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
