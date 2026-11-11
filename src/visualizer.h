#pragma once

#include <QList>
#include <QLocalServer>
#include <QObject>
#include <QString>
#include <vector>

#include "device.h"
#include "spectrum.h"
#include "spectrumrenderer.h"
#include "streamsession.h"
#include "themeprofile.h"

class AudioSource;
class Layout;
class QLocalSocket;

// The visualizer's socket, in the runtime directory. One line per message:
//   ping    -> ok
//   reload  -> ok      the applied profile changed: read it again
//   frames  -> ok      then "frame <hex>" for every frame shown, <hex> being hid R G B per key
//   other   -> error
QString visualizerSocketPath(const QString &runtimeDir);
// Where the service leaves the reason it stopped on an error, for `klc-light visualizer on`.
QString visualizerErrorPath(const QString &runtimeDir);
// Sends one line and returns the one-line answer; empty when nothing is listening. Waits
// half a second at most.
QString visualizerRequest(const QString &socketPath, const QString &line);

// The music visualizer: shows the spectrum of what the computer plays on the keyboard, in
// the applied profile's colors, until stopped; then the profile is back.
class VisualizerService : public QObject {
    Q_OBJECT

public:
    static constexpr int FrameMs = 33; // GG's PrismSync step

    VisualizerService(Device &device, AudioSource &audio, const Layout &layout, QString libraryDir, ThemePaths paths,
                      QString socketPath, QObject *parent = nullptr);
    ~VisualizerService() override;

    // Refuses without a profile to show and to give back. The sound is opened first, so a
    // failure there leaves the keyboard untouched.
    ApplyResult start();
    // One frame: called every FrameMs.
    void tick();
    // Puts the profile back (black if the keyboard was turned off), detaches, closes the
    // socket. The error, if the profile could not be put back.
    ApplyResult stop();
    bool running() const { return m_session.running(); }

public slots:
    // logind's PrepareForSleep. Before sleeping the profile goes back and the keyboard is
    // detached, the state it is known to sleep well in; the socket stays. After waking the
    // stream starts again. Holding the controller attached across suspend was never measured.
    void prepareForSleep(bool sleeping);

signals:
    // A send was refused, or the keyboard did not come back after waking. It is already
    // detached and nothing more is sent.
    void failed(const ApplyResult &result);
    // The keyboard is detached and the computer may sleep; the stream is back after waking.
    void asleep();
    void awake();

private:
    QString loadProfile(); // empty, or why there is nothing to show
    void onConnection();
    void onLine(QLocalSocket *client, const QByteArray &line);
    void closeSocket();

    void beginStream();

    Device &m_device;
    AudioSource &m_audio;
    const Layout &m_layout;
    QString m_libraryDir;
    ThemePaths m_paths;
    QString m_socketPath;
    Spectrum m_spectrum;
    SpectrumRenderer m_renderer;
    StreamSession m_session;
    QList<Packet> m_restore;
    Frame m_lastFrame;
    QLocalServer m_server;
    QList<QLocalSocket *> m_clients;
    QList<QLocalSocket *> m_subscribers;
    std::vector<float> m_samples;
    bool m_asleep = false;
    int m_emptyTicks = 0; // frames in a row for which no sample arrived
};

// `klc-light visualizer run`: the service's process. Exit codes as `apply`.
int runVisualizer(int argc, char *argv[]);
