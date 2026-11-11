#include "cli.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "device.h"
#include <QFile>

#include "document.h"
#include "layout.h"
#include "library.h"
#include "protocol.h"
#include "version.h"
#include "visualizer.h"

namespace {

QByteArray readFileOrEmpty(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

int finish(CliEnv &env, bool json, bool ok, const QString &message, int packets = 0, const QString &source = {}) {
    if (json) {
        QJsonObject result{{"ok", ok}, {"packets", packets}, {"source", source}};
        if (!message.isEmpty())
            result[ok ? "message" : "error"] = message;
        *env.out << QJsonDocument(result).toJson(QJsonDocument::Compact) << "\n";
    } else if (!message.isEmpty()) {
        *(ok ? env.out : env.err) << message << "\n";
    }
    return ok ? 0 : 1;
}

// The library profile to send when none is named: the applied one, or the active theme's
// when asked for or when the applied one is stale (deleted since). Empty if there is none.
QString defaultProfileName(const Library &lib, const ThemePaths &paths, bool themeOnly) {
    if (!themeOnly && lib.names().contains(lib.applied(), Qt::CaseInsensitive))
        return lib.applied();
    const QString active = QString::fromUtf8(readFileOrEmpty(paths.themeName)).trimmed();
    return lib.profileForTheme(active);
}

} // namespace

ResolvedProfile resolveProfile(const QString &libraryDir, const ThemePaths &paths) {
    ResolvedProfile resolved;
    Library lib(libraryDir);
    lib.migrate(paths);
    resolved.off = lib.off();
    const QString name = defaultProfileName(lib, paths, false);
    if (name.isEmpty())
        return resolved;
    if (const auto doc = lib.load(name, &resolved.errors)) {
        resolved.profile = lighting(*doc);
        resolved.name = doc->name;
    }
    return resolved;
}

// `klc-light list [--json]`: the library, the applied profile and the active theme, for the
// Omarchy bar widget and scripts. Sends nothing.
int runList(const QStringList &args, CliEnv &env) {
    const bool json = args.contains("--json");
    Library lib(env.libraryDir);
    lib.migrate(env.paths);
    QString currentTheme;
    QFile name(env.paths.themeName);
    if (name.open(QIODevice::ReadOnly))
        currentTheme = QString::fromUtf8(name.readAll()).trimmed();
    QJsonArray profiles;
    for (const LibraryEntry &entry : lib.entries())
        profiles.append(QJsonObject{{"name", entry.name}, {"theme", lib.themeOf(entry.name)}, {"unreadable", !entry.readable}});
    if (json) {
        // The service manager's word, not a file's: a killed visualizer is not reported as running.
        const bool visualizer = env.systemctl && env.systemctl({"is-active", "--quiet", kVisualizerUnit}) == 0;
        const QJsonObject out{{"applied", lib.applied()}, {"off", lib.off()}, {"visualizer", visualizer},
                              {"currentTheme", currentTheme}, {"profiles", profiles}};
        *env.out << QJsonDocument(out).toJson(QJsonDocument::Compact) << "\n";
    } else {
        for (const QJsonValue &p : profiles) {
            const QString n = p.toObject().value("name").toString();
            *env.out << (n == lib.applied() ? "* " : "  ") << n << "\n";
        }
    }
    return 0;
}

static const char kUsage[] = "usage: klc-light [open] | klc-light apply [<name> | <file.json>] [--theme] [--check] [--json]\n"
                      "       klc-light list [--json] | klc-light off [--check] [--json]\n"
                      "       klc-light restore [--check] [--json]\n"
                      "       klc-light visualizer on|off [--json]\n"
                      "       klc-light --version | klc-light --help\n";

// `klc-light visualizer on|off [--json]`: starts and stops the visualizer's user unit. `on`
// returns once the stream has begun, or with the reason it could not.
static int runVisualizerCommand(const QStringList &args, CliEnv &env) {
    const QString word = args.value(0);
    const bool json = args.mid(1) == QStringList{"--json"};
    if ((word != "on" && word != "off") || (args.size() > 1 && !json)) {
        *env.err << kUsage;
        return 1;
    }
    if (!env.systemctl)
        return finish(env, json, false, "no service manager", 0, "visualizer");
    if (word == "off") {
        // Stopping a stopped unit succeeds too.
        const bool stopped = env.systemctl({"stop", kVisualizerUnit}) == 0;
        return finish(env, json, stopped, stopped ? QString() : QString("The visualizer could not be stopped."), 0, "visualizer");
    }
    if (env.systemctl({"start", kVisualizerUnit}) == 0)
        return finish(env, json, true, QString(), 0, "visualizer");
    // The service's own exit code says whether the controller is gone, as `apply` would.
    const bool absent = env.systemctlOutput
                        && env.systemctlOutput({"show", "-p", "ExecMainStatus", "--value", kVisualizerUnit}).trimmed() == "2";
    finish(env, json, false,
           absent ? keyboardAbsentMessage() : QString("The visualizer could not start; see journalctl --user -u klc-light-visualizer."),
           0, "visualizer");
    return absent ? 2 : 1;
}

int runCli(const QStringList &args, CliEnv &env) {
    if (args.value(0) == "list")
        return runList(args.mid(1), env);
    if (args.value(0) == "visualizer")
        return runVisualizerCommand(args.mid(1), env);
    // `off` sends every key black through the same path as `apply`, and keeps `applied`.
    // `restore` (the boot hook) puts the keyboard back as it was left: black if it was
    // turned off, else the applied profile, as `apply` would.
    const bool restore = args.value(0) == "restore";
    const bool takesProfile = args.value(0) == "apply";
    bool off = args.value(0) == "off";
    if (args.value(0) == "--version") {
        *env.out << "klc-light " << kVersion << "\n";
        return 0;
    }
    if (!takesProfile && !off && !restore) {
        const bool help = args.value(0) == "--help" || args.value(0) == "-h";
        *(help ? env.out : env.err) << kUsage;
        return help ? 0 : 1;
    }
    bool check = false, json = false;
    QString target;
    bool theme = false;
    for (const QString &arg : args.mid(1)) {
        if (arg == "--check")
            check = true;
        else if (arg == "--json")
            json = true;
        else if (arg == "--theme")
            theme = true;
        else if (arg == "--experimental-reactive")
            ; // Needed before reactive lighting was validated; still accepted, now a no-op.
        else if (arg.startsWith("--") || !takesProfile)
            return finish(env, json, false, QString("unknown option %1").arg(arg));
        else
            target = arg;
    }

    // Reads and builds the profile to send: the given file, the named library profile, or the
    // one the active theme is linked to. Everything is read here, at send time, so the last
    // theme of a quick series of switches is the one that wins.
    QString source;
    QString libraryName; // set when the profile comes from the library
    auto build = [&]() {
        BuildResult built;
        std::optional<Profile> profile;
        libraryName.clear();
        source.clear();
        Library lib(env.libraryDir);
        lib.migrate(env.paths);
        if (restore)
            off = lib.off();
        if (off) {
            Profile black;
            black.base.color = Qt::black;
            profile = black;
            source = "off";
        } else if (!target.isEmpty() && (target.contains('/') || target.endsWith(".json", Qt::CaseInsensitive))) {
            const auto doc = readDocument(target, &built.errors);
            if (doc)
                profile = lighting(*doc);
            source = target;
        } else {
            const QString name = target.isEmpty() ? defaultProfileName(lib, env.paths, theme) : target;
            // No profile for the theme is not an error: there is just nothing to send.
            if (!name.isEmpty()) {
                const auto doc = lib.load(name, &built.errors);
                if (doc) {
                    profile = lighting(*doc);
                    libraryName = doc->name;
                    source = doc->name;
                }
            }
        }
        if (built.errors.isEmpty() && profile)
            built = buildPackets(*profile, Layout::gs76());
        return built;
    };

    if (check) {
        const BuildResult built = build();
        if (!built.errors.isEmpty())
            return finish(env, json, false, built.errors.join("\n"), 0, source);
        return finish(env, json, true, built.packets.isEmpty() ? QString() : QString("%1 packets OK").arg(built.packets.size()),
                      int(built.packets.size()), source);
    }

    // While the visualizer runs it owns the keyboard: nothing is sent here. The profile is
    // recorded as a send would record it, and the visualizer told, so it takes the new
    // colors now and gives this profile back when it stops.
    if (!env.visualizerSocket.isEmpty() && visualizerRequest(env.visualizerSocket, "ping") == "ok") {
        if (restore)
            return finish(env, json, true, QString());
        const BuildResult built = build();
        if (!built.errors.isEmpty())
            return finish(env, json, false, built.errors.join("\n"), 0, source);
        if (built.packets.isEmpty())
            return finish(env, json, true, QString(), 0, source);
        if (!off && libraryName.isEmpty())
            return finish(env, json, false, "The visualizer is running: stop it (klc-light visualizer off) to apply a profile file.",
                          0, source);
        Library lib(env.libraryDir);
        lib.setOff(off);
        if (!libraryName.isEmpty())
            lib.setApplied(libraryName);
        visualizerRequest(env.visualizerSocket, "reload");
        return finish(env, json, true, QString(), 0, source);
    }

    if (env.fake) {
        const BuildResult built = build();
        if (!built.errors.isEmpty())
            return finish(env, json, false, built.errors.join("\n"), 0, source);
        for (const Packet &packet : built.packets)
            *env.out << (packet.kind == Packet::Feature ? "F " : "O ") << packet.data.toHex() << "\n";
        return finish(env, json, true, QString(), int(built.packets.size()), source);
    }

    // The theme is read again once it is this send's turn, so the last theme of a quick
    // series of switches is the one that ends up on the keyboard.
    int count = 0;
    const ApplyResult result = env.device->applyLatest([&] {
        BuildResult built = build();
        count = int(built.packets.size());
        return built;
    });
    if (result.error != ApplyError::None) {
        finish(env, json, false, result.message, count, source);
        // A distinct status lets the theme hooks tell the user to power off.
        return result.error == ApplyError::KeyboardAbsent ? 2 : 1;
    }
    if (count > 0) {
        // Any successful send turns the keyboard back on, except `off` itself.
        Library lib(env.libraryDir);
        lib.setOff(off);
    }
    if (!libraryName.isEmpty() && count > 0) {
        Library lib(env.libraryDir);
        lib.setApplied(libraryName);
    }
    return finish(env, json, true, QString(), count, source);
}
