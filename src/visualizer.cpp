#include "visualizer.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLocalSocket>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <csignal>
#include <sys/socket.h>
#include <systemd/sd-daemon.h>
#include <unistd.h>

#include "audiosource.h"
#include "cli.h"
#include "hidapitransport.h"
#include "layout.h"
#include "pipewiresource.h"
#include "printdevice.h"
#include "sysfsbinder.h"
#include "translator.h"

QString visualizerSocketPath(const QString &runtimeDir) { return runtimeDir + "/klc-light-visualizer.sock"; }
QString visualizerErrorPath(const QString &runtimeDir) { return runtimeDir + "/klc-light-visualizer.error"; }

QString visualizerRequest(const QString &socketPath, const QString &line) {
    constexpr int kWaitMs = 500;
    QLocalSocket socket;
    socket.connectToServer(socketPath);
    if (!socket.waitForConnected(kWaitMs))
        return {};
    socket.write(line.toUtf8() + "\n");
    QElapsedTimer timer;
    timer.start();
    while (!socket.canReadLine()) {
        const int left = kWaitMs - int(timer.elapsed());
        if (left <= 0 || !socket.waitForReadyRead(left))
            return {};
    }
    return QString::fromUtf8(socket.readLine()).trimmed();
}

VisualizerService::VisualizerService(Device &device, AudioSource &audio, const Layout &layout, QString libraryDir,
                                     ThemePaths paths, QString socketPath, QObject *parent)
    : QObject(parent), m_device(device), m_audio(audio), m_layout(layout), m_libraryDir(std::move(libraryDir)), m_paths(std::move(paths)),
      m_socketPath(std::move(socketPath)), m_spectrum(audio.sampleRate(), SpectrumRenderer::Columns), m_renderer(layout),
      m_session(device, layout), m_samples(size_t(audio.sampleRate())) {
    qRegisterMetaType<ApplyResult>();
    connect(&m_server, &QLocalServer::newConnection, this, &VisualizerService::onConnection);
}

VisualizerService::~VisualizerService() { stop(); }

QString VisualizerService::loadProfile() {
    const ResolvedProfile resolved = resolveProfile(m_libraryDir, m_paths);
    if (!resolved.errors.isEmpty())
        return resolved.errors.join("\n");
    if (!resolved.profile)
        return QCoreApplication::translate("Visualizer", "Apply a profile before starting the visualizer.");
    Profile black;
    black.base.color = Qt::black;
    const BuildResult built = buildPackets(resolved.off ? black : *resolved.profile, m_layout);
    if (!built.errors.isEmpty())
        return built.errors.join("\n");
    m_restore = built.packets;
    m_renderer.setProfile(*resolved.profile);
    return {};
}

ApplyResult VisualizerService::start() {
    if (m_session.running())
        return {};
    if (const QString problem = loadProfile(); !problem.isEmpty())
        return {ApplyError::Invalid, problem};
    QString audioError;
    if (!m_audio.start(&audioError))
        return {ApplyError::Invalid, QCoreApplication::translate("Visualizer", "Could not listen to the sound: %1").arg(audioError)};
    if (const ApplyResult started = m_session.start(); started.error != ApplyError::None) {
        m_audio.stop();
        return started;
    }
    beginStream();
    // A socket file left by a killed service would keep this one from listening.
    QLocalServer::removeServer(m_socketPath);
    if (!m_server.listen(m_socketPath)) {
        const QString problem = m_server.errorString();
        stop();
        return {ApplyError::Invalid, problem};
    }
    return {};
}

void VisualizerService::beginStream() {
    m_spectrum.reset();
    m_lastFrame.clear();
    m_emptyTicks = 0;
}

void VisualizerService::tick() {
    if (!m_session.running())
        return;
    int count = m_audio.read(m_samples.data(), int(m_samples.size()));
    // Silence arrives as zeros. No samples at all means the sound server stopped delivering:
    // after a moment that counts as silence too, or the last bars would stay lit for good.
    constexpr int kStalledTicks = 5;
    m_emptyTicks = count > 0 ? 0 : m_emptyTicks + 1;
    if (m_emptyTicks >= kStalledTicks) {
        count = std::min(int(m_samples.size()), m_audio.sampleRate() * FrameMs / 1000);
        std::fill_n(m_samples.begin(), count, 0.0f);
    }
    m_spectrum.push(m_samples.data(), count);
    const Frame frame = m_renderer.render(m_spectrum.frame());
    if (frame == m_lastFrame)
        return;
    const ApplyResult shown = m_session.show(frame);
    if (shown.error != ApplyError::None) {
        m_audio.stop();
        closeSocket();
        emit failed(shown);
        return;
    }
    m_lastFrame = frame;
    if (m_subscribers.isEmpty())
        return;
    QByteArray bytes;
    bytes.reserve(frame.size() * 4);
    for (const KeyColor &key : frame) {
        bytes.append(char(key.hid));
        bytes.append(char(key.color.red()));
        bytes.append(char(key.color.green()));
        bytes.append(char(key.color.blue()));
    }
    const QByteArray line = "frame " + bytes.toHex() + "\n";
    for (QLocalSocket *subscriber : std::as_const(m_subscribers))
        subscriber->write(line);
}

ApplyResult VisualizerService::stop() {
    ApplyResult restored;
    if (m_session.running()) {
        restored = m_session.stop(m_restore);
        m_audio.stop();
    }
    m_asleep = false;
    closeSocket();
    return restored;
}

void VisualizerService::prepareForSleep(bool sleeping) {
    if (sleeping) {
        if (!m_session.running())
            return;
        const ApplyResult restored = m_session.stop(m_restore);
        m_audio.stop();
        // Detached either way: the computer may sleep.
        if (restored.error != ApplyError::None) {
            // The controller refused a send: nothing more goes to it, waking included.
            closeSocket();
            emit asleep();
            emit failed(restored);
            return;
        }
        m_asleep = true;
        emit asleep();
        return;
    }
    if (!m_asleep)
        return;
    m_asleep = false;
    // The profile may have changed while the stream was down.
    loadProfile();
    beginStream();
    // The clock ran during the sleep, but the controller has only just come back.
    m_device.restartInterval();
    QString audioError;
    ApplyResult resumed;
    if (!m_audio.start(&audioError))
        resumed = {ApplyError::Invalid, QCoreApplication::translate("Visualizer", "Could not listen to the sound: %1").arg(audioError)};
    else if (resumed = m_session.start(); resumed.error != ApplyError::None)
        m_audio.stop();
    if (resumed.error != ApplyError::None) {
        closeSocket();
        emit failed(resumed);
        return;
    }
    emit awake();
}

void VisualizerService::closeSocket() {
    m_server.close();
    const QList<QLocalSocket *> clients = m_clients;
    m_clients.clear();
    m_subscribers.clear();
    for (QLocalSocket *client : clients) {
        client->disconnect(this);
        client->disconnectFromServer();
        client->deleteLater();
    }
}

void VisualizerService::onConnection() {
    while (QLocalSocket *client = m_server.nextPendingConnection()) {
        m_clients.append(client);
        connect(client, &QLocalSocket::readyRead, this, [this, client] {
            while (client->canReadLine())
                onLine(client, client->readLine().trimmed());
        });
        connect(client, &QLocalSocket::disconnected, this, [this, client] {
            m_clients.removeAll(client);
            m_subscribers.removeAll(client);
            client->deleteLater();
        });
    }
}

void VisualizerService::onLine(QLocalSocket *client, const QByteArray &line) {
    if (line == "ping") {
        client->write("ok\n");
    } else if (line == "reload") {
        // A profile that cannot be read keeps the one in use.
        loadProfile();
        m_lastFrame.clear();
        client->write("ok\n");
    } else if (line == "frames") {
        if (!m_subscribers.contains(client))
            m_subscribers.append(client);
        client->write("ok\n");
    } else {
        client->write("error\n");
    }
    client->flush();
}

namespace {

int g_signalPipe[2] = {-1, -1};
void onSignal(int) {
    const char byte = 1;
    if (write(g_signalPipe[1], &byte, 1) < 0) {
        // Nothing to do in a signal handler; the next signal tries again.
    }
}

int exitCodeOf(const ApplyResult &result) {
    if (result.error == ApplyError::None)
        return 0;
    return result.error == ApplyError::KeyboardAbsent ? 2 : 1;
}

} // namespace

int runVisualizer(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    Translator translator;
    app.installTranslator(&translator);

    QTextStream out(stdout), err(stderr);
    const bool fake = qEnvironmentVariableIntValue("KLC_LIGHT_FAKE_DEVICE") == 1;
    HidapiTransport hidTransport;
    SysfsBinder sysfsBinder;
    PrintTransport printTransport(out);
    NullBinder nullBinder;
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty())
        runtime = QDir::tempPath();
    Device device(fake ? static_cast<Transport &>(printTransport) : hidTransport,
                  fake ? static_cast<Binder &>(nullBinder) : sysfsBinder, runtime, [](int ms) { QThread::msleep(ms); });
    PipewireSource audio;
    VisualizerService service(device, audio, Layout::gs76(), QDir::homePath() + "/.config/klc-light", ThemePaths::standard(),
                              visualizerSocketPath(runtime));

    // SIGTERM (systemd's stop) and SIGINT end the stream in the event loop, where the
    // profile can be put back before detaching.
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalPipe) != 0) {
        err << "klc-light: cannot set up signal handling\n";
        return 1;
    }
    QSocketNotifier signalNotifier(g_signalPipe[0], QSocketNotifier::Read);
    const QString errorPath = visualizerErrorPath(runtime);
    QFile::remove(errorPath);
    // The journal gets the reason, and so does `klc-light visualizer on`, through a file.
    auto report = [&err, errorPath](const ApplyResult &result) {
        err << result.message << "\n";
        err.flush();
        QFile file(errorPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(result.message.toUtf8() + "\n");
        return exitCodeOf(result);
    };
    QObject::connect(&signalNotifier, &QSocketNotifier::activated, &app, [&] {
        signalNotifier.setEnabled(false);
        sd_notify(0, "STOPPING=1");
        const ApplyResult stopped = service.stop();
        app.exit(stopped.error == ApplyError::None ? 0 : report(stopped));
    });
    struct sigaction action {};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);

    const ApplyResult started = service.start();
    if (started.error != ApplyError::None)
        return report(started);
    QObject::connect(&service, &VisualizerService::failed, &app,
                     [&](const ApplyResult &result) { app.exit(report(result)); });
    // The computer waits for the keyboard to be detached before sleeping: a delay lock is
    // held while the stream runs and dropped once the service is ready to sleep.
    QDBusUnixFileDescriptor sleepLock;
    auto takeSleepLock = [&sleepLock] {
        QDBusInterface login("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                             QDBusConnection::systemBus());
        const QDBusReply<QDBusUnixFileDescriptor> reply =
            login.call("Inhibit", "sleep", "KLC Light", "Detaching the keyboard lighting controller", "delay");
        sleepLock = reply.isValid() ? reply.value() : QDBusUnixFileDescriptor();
    };
    takeSleepLock();
    QDBusConnection::systemBus().connect("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                                         "PrepareForSleep", &service, SLOT(prepareForSleep(bool)));
    QObject::connect(&service, &VisualizerService::asleep, &app, [&sleepLock] { sleepLock = QDBusUnixFileDescriptor(); });
    QObject::connect(&service, &VisualizerService::awake, &app, takeSleepLock);

    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, &service, &VisualizerService::tick);
    timer.start(VisualizerService::FrameMs);
    sd_notify(0, "READY=1");
    return app.exec();
}
