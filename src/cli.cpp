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

    std::optional<Profile> profile;
    QString source;
    QStringList errors;
    if (!path.isEmpty()) {
        profile = readProfile(path, &errors);
        source = path;
    } else {
        const Resolved resolved = resolveProfile(env.paths);
        profile = resolved.profile;
        source = resolved.source;
        errors = resolved.errors;
    }
    if (!errors.isEmpty())
        return finish(env, json, false, errors.join("\n"), 0, source);
    // A theme without keyboard lighting is not an error: there is just nothing to send.
    if (!profile)
        return finish(env, json, true, QString(), 0, source);

    const BuildResult built = buildPackets(*profile, Layout::gs76(), reactive);
    if (!built.errors.isEmpty())
        return finish(env, json, false, built.errors.join("\n"), 0, source);
    if (check)
        return finish(env, json, true, QString("%1 packets OK").arg(built.packets.size()), int(built.packets.size()), source);

    const ApplyResult result = env.device->apply(built.packets);
    if (result.error != ApplyError::None)
        return finish(env, json, false, result.message, int(built.packets.size()), source);
    return finish(env, json, true, QString(), int(built.packets.size()), source);
}
