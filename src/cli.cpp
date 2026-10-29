#include "cli.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "device.h"
#include "layout.h"
#include "protocol.h"

namespace {

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

} // namespace

int runCli(const QStringList &args, CliEnv &env) {
    if (args.value(0) != "apply") {
        *env.err << "usage: klc-light [open] | klc-light apply [profile.json] [--check] [--json] [--experimental-reactive]\n";
        return 1;
    }
    bool check = false, json = false, reactive = false;
    QString path;
    for (const QString &arg : args.mid(1)) {
        if (arg == "--check")
            check = true;
        else if (arg == "--json")
            json = true;
        else if (arg == "--experimental-reactive")
            reactive = true;
        else if (arg.startsWith("--"))
            return finish(env, json, false, QString("unknown option %1").arg(arg));
        else
            path = arg;
    }

    // Reads and builds the profile to send: the given file, or the active theme's.
    QString source;
    auto build = [&]() {
        BuildResult built;
        std::optional<Profile> profile;
        if (!path.isEmpty()) {
            profile = readProfile(path, &built.errors);
            source = path;
        } else {
            const Resolved resolved = resolveProfile(env.paths);
            profile = resolved.profile;
            source = resolved.source;
            built.errors = resolved.errors;
        }
        // A theme without keyboard lighting is not an error: there is just nothing to send.
        if (built.errors.isEmpty() && profile)
            built = buildPackets(*profile, Layout::gs76(), reactive);
        return built;
    };

    if (check) {
        const BuildResult built = build();
        if (!built.errors.isEmpty())
            return finish(env, json, false, built.errors.join("\n"), 0, source);
        return finish(env, json, true, built.packets.isEmpty() ? QString() : QString("%1 packets OK").arg(built.packets.size()),
                      int(built.packets.size()), source);
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
    return finish(env, json, true, QString(), count, source);
}
