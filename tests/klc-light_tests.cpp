#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>

#include "backend.h"
#include "cli.h"
#include "device.h"
#include "sudobinder.h"
#include "effects.h"
#include "keylabels.h"
#include "layout.h"
#include "preview.h"
#include "profile.h"
#include "protocol.h"
#include "themeprofile.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "theme.h"
#include "translator.h"


// Records what the device code does instead of touching hardware.
struct FakeLog {
    QStringList events;
    qint64 now = 1'000'000;
    QList<int> sleeps;
    QList<QByteArray> packets;
};

class FakeTransport : public Transport {
public:
    explicit FakeTransport(FakeLog &log) : m_log(log) {}
    bool waitAndOpen(int) override { m_log.events << "open"; return openOk; }
    bool sendFeature(const QByteArray &data) override { m_log.packets << data; return send(); }
    bool sendOutput(const QByteArray &data) override { m_log.packets << data; return send(); }
    void close() override { m_log.events << "close"; }
    QString lastError() const override { return "fake error"; }
    bool openOk = true;
    int failAt = -1;
    int sent = 0;


private:

    bool send() {
        m_log.events << "send";
        return ++sent != failAt;
    }
    FakeLog &m_log;
};

class FakeBinder : public Binder {
public:
    explicit FakeBinder(FakeLog &log) : m_log(log) {}
    bool bind(QString *error) override {
        m_log.events << "bind";
        if (!bindOk)
            *error = bindError;
        return bindOk;
    }
    bool unbind(QString *) override { m_log.events << "unbind"; return true; }
    bool bindOk = true;
    QString bindError = "sudo: a password is required";

private:
    FakeLog &m_log;
};

class KlcLightTests : public QObject {
    Q_OBJECT

private slots:
    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

    void layoutCounts()
    {
        const Layout &layout = Layout::gs76();
        QCOMPARE(layout.keys().size(), 101);
        for (Region region : {Region::Alphanum, Region::Enter, Region::Modifiers, Region::Numpad})
            QCOMPARE(layout.regionHids(region).size(), 42);
        QCOMPARE(layout.aquaHids(Region::Alphanum).size(), 42);
        QCOMPARE(layout.aquaHids(Region::Enter).size(), 11);
        QCOMPARE(layout.aquaHids(Region::Modifiers).size(), 24);
        QCOMPARE(layout.aquaHids(Region::Numpad).size(), 36);
        QSet<quint8> aqua;
        for (Region region : {Region::Alphanum, Region::Enter, Region::Modifiers, Region::Numpad})
            for (quint8 hid : layout.aquaHids(region))
                aqua.insert(hid);
        for (const Key &key : layout.keys())
            QVERIFY2(aqua.contains(key.hid), qPrintable(key.id));
        QCOMPARE(int(layout.byId("KEY_Q")->hid), 0x14);
        QCOMPARE(layout.byHid(0x14)->id, QString("KEY_Q"));
        QCOMPARE(regionCode(Region::Alphanum), quint8(0x2a));
        QCOMPARE(regionCode(Region::Enter), quint8(0x0b));
        QCOMPARE(regionCode(Region::Modifiers), quint8(0x18));
        QCOMPARE(regionCode(Region::Numpad), quint8(0x24));
    }

    void layoutGroups()
    {
        const Layout &layout = Layout::gs76();
        QCOMPARE(layout.group("wasd"), QStringList({"KEY_W", "KEY_A", "KEY_S", "KEY_D"}));
        QCOMPARE(layout.group("function").size(), 12);
        QCOMPARE(layout.group("digits").size(), 10);
        QCOMPARE(layout.group("all").size(), 101);
        QVERIFY(layout.group("numpad").contains("KEY_KP5"));
    }

    void labelsFrench()
    {
        const Layout &layout = Layout::gs76();
        const KeyLabels fr = KeyLabels::fromRmlvo("fr", "");
        QCOMPARE(fr.label(*layout.byId("KEY_Q")), QString("A"));
        QCOMPARE(fr.label(*layout.byId("KEY_W")), QString("Z"));
        QCOMPARE(fr.label(*layout.byId("KEY_ESC")), QString("Esc"));
        QCOMPARE(fr.label(*layout.byId("KEY_LEFTBRACE")), QString("^"));
        QCOMPARE(fr.label(*layout.byId("KEY_SCROLLLOCK")), QString("ScrLk"));
        QCOMPARE(fr.label(*layout.byId("KEY_FN")), QString("Fn"));
        QCOMPARE(fr.label(*layout.byId("KEY_BACKSLASH")), QString("*"));
        QCOMPARE(fr.label(*layout.byId("KEY_102ND")), QString("<"));
        QCOMPARE(fr.label(*layout.byId("KEY_KP7")), QString("7"));
        QCOMPARE(fr.label(*layout.byId("KEY_KPDOT")), QString("."));
        const KeyLabels us = KeyLabels::fromRmlvo("us", "");
        QCOMPARE(us.label(*layout.byId("KEY_Q")), QString("Q"));
    }

    void profileRoundTrip()
    {
        const QByteArray json = R"({
          "version": 1, "brightness": 100, "speed": 100,
          "base": { "type": "static", "color": "#00E5FF" },
          "keys": { "KEY_W": "#FF0000", "KEY_A": "#FF0000" },
          "effects": [
            { "name": "vague", "type": "wave", "keys": ["KEY_F1", "KEY_F2"],
              "steps": [["#FF0000", 14], ["#FFAA00", 10]], "origin": "aqua-1" }
          ],
          "reactive": { "enabled": false, "color": "#FFFFFF", "duration": 30 }
        })";
        const QJsonObject source = QJsonDocument::fromJson(json).object();
        QStringList errors;
        const auto profile = profileFromJson(source, &errors);
        QVERIFY2(profile, qPrintable(errors.join("; ")));
        QVERIFY(validate(*profile, Layout::gs76()).isEmpty());
        QCOMPARE(toJson(*profile), source);
        QCOMPARE(profile->effects.first().start, QColor("#FF0000"));
    }

    void profileRejects_data()
    {
        QTest::addColumn<QString>("patch");
        QTest::addColumn<QString>("expected");
        QTest::newRow("unknown key") << R"({"keys": {"KEY_NOPE": "#FF0000"}})" << "KEY_NOPE";
        QTest::newRow("version") << R"({"version": 2})" << "version";
        QTest::newRow("bad color") << R"({"keys": {"KEY_Q": "#12345"}})" << "#12345";
        QTest::newRow("brightness") << R"({"brightness": 0})" << "brightness";
        QTest::newRow("speed") << R"({"speed": 500})" << "speed";
        QTest::newRow("missing base") << R"({"base": null})" << "base";
        QTest::newRow("too many steps") << "STEPS17" << "16";
        QTest::newRow("too many effects") << "EFFECTS7" << "6";
        QTest::newRow("key in two effects") << "TWICE" << "KEY_F1";
    }

    void profileRejects()
    {
        QFETCH(QString, patch);
        QFETCH(QString, expected);
        QJsonObject object{
            {"version", 1}, {"brightness", 100}, {"speed", 100},
            {"base", QJsonObject{{"type", "static"}, {"color", "#000000"}}},
        };
        auto effect = [](const QString &key, int steps) {
            QJsonArray list;
            for (int i = 0; i < steps; ++i)
                list.append(QJsonArray{i % 2 ? "#000000" : "#FF0000", 10});
            return QJsonObject{{"name", "e"}, {"type", "wave"}, {"keys", QJsonArray{key}},
                               {"steps", list}, {"origin", "aqua-1"}};
        };
        if (patch == "STEPS17")
            object["effects"] = QJsonArray{effect("KEY_F1", 17)};
        else if (patch == "EFFECTS7") {
            QJsonArray effects;
            for (int i = 1; i <= 7; ++i)
                effects.append(effect(QString("KEY_F%1").arg(i), 2));
            object["effects"] = effects;
        } else if (patch == "TWICE")
            object["effects"] = QJsonArray{effect("KEY_F1", 2), effect("KEY_F1", 2)};
        else {
            const QJsonObject extra = QJsonDocument::fromJson(patch.toUtf8()).object();
            for (auto it = extra.begin(); it != extra.end(); ++it) {
                if (it.value().isNull())
                    object.remove(it.key());
                else
                    object[it.key()] = it.value();
            }
        }
        QStringList errors;
        const auto profile = profileFromJson(object, &errors);
        if (profile)
            errors += validate(*profile, Layout::gs76());
        QVERIFY2(!errors.isEmpty(), "profile was accepted");
        QVERIFY2(errors.join("\n").contains(expected), qPrintable(errors.join("\n")));
    }

    void effectsAresMatchesPython()
    {
        QByteArray wave, pulse;
        for (const auto &[kind, data] : fixture("ares.hex"))
            if (kind == 'F' && quint8(data.at(0)) == 0x0b)
                (data.at(2) == 0 ? pulse : wave) = data;
        QString error;
        QCOMPARE(transitions(builtinPalette("ares", false), &error), table(wave));
        QCOMPARE(transitions(builtinPalette("ares", true), &error), table(pulse));
        QVERIFY(error.isEmpty());
    }

    void effectsCloses()
    {
        const EffectDef def{QColor("#010101"), {{QColor("#FE00FF"), 10}, {QColor("#030303"), 10}}, 10, "aqua-1"};
        QString error;
        const QList<Transition> list = transitions(def, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        int r = 0, g = 0, b = 0;
        for (const Transition &t : list) {
            r += t.dr;
            g += t.dg;
            b += t.db;
        }
        QCOMPARE(r, 0);
        QCOMPARE(g, 0);
        QCOMPARE(b, 0);
    }

    void effectsScaled()
    {
        const EffectDef def{QColor("#FF0000"), {{QColor("#FF0000"), 40}, {QColor("#000000"), 1}}, 40, "aqua-0"};
        QCOMPARE(scaled(def, 50, 100).start, QColor("#800000"));
        QCOMPARE(scaled(def, 100, 200).steps.first().duration, 20);
        QCOMPARE(scaled(def, 100, 200).closing, 20);
        QCOMPARE(scaled(def, 100, 400).steps.last().duration, 1);
    }

    void effectsTooMany()
    {
        EffectDef def{Qt::red, {}, 10, "aqua-1"};
        for (int i = 0; i < 17; ++i)
            def.steps.append({i % 2 ? Qt::black : Qt::red, 10});
        QString error;
        transitions(def, &error);
        QVERIFY2(error.contains("16"), qPrintable(error));
    }

    void effectsBaseTypes()
    {
        BaseLayer base;
        base.type = "breathing";
        base.color = QColor("#00E5FF");
        const EffectDef breathing = baseEffect(base);
        QCOMPARE(breathing.origin, QString("aqua-0"));
        QCOMPARE(breathing.steps.size(), 2);
        base.type = "rainbow";
        QCOMPARE(baseEffect(base).steps.size(), 5);
        QCOMPARE(baseEffect(base).origin, QString("aqua-1"));
    }

    void protocolStaticAll()
    {
        Profile profile;
        profile.base.color = QColor("#00E5FF");
        compareWithFixture(buildPackets(profile, Layout::gs76()), "static-00e5ff.hex");
    }

    void protocolStaticZqsd()
    {
        Profile profile;
        profile.base.color = QColor("#00E5FF");
        for (const QString &id : {"KEY_Q", "KEY_W", "KEY_E", "KEY_A", "KEY_S", "KEY_D"})
            profile.keys.insert(id, QColor("#FF0000"));
        compareWithFixture(buildPackets(profile, Layout::gs76()), "static-zqsd.hex");
    }

    void protocolAres() { compareWithFixture(buildPackets(builtinProfile("ares"), Layout::gs76()), "ares.hex"); }
    void protocolTron() { compareWithFixture(buildPackets(builtinProfile("tron"), Layout::gs76()), "tron.hex"); }

    void protocolNoReactiveByDefault()
    {
        Profile profile;
        profile.reactive.enabled = true;
        const BuildResult result = buildPackets(profile, Layout::gs76());
        QVERIFY(result.errors.isEmpty());
        for (const Packet &packet : result.packets)
            if (packet.kind == Packet::Feature && quint8(packet.data.at(0)) == 0x0e)
                for (int i = 0; i < 42; ++i)
                    QVERIFY(quint8(packet.data.at(4 + i * 12 + 9)) != 8);
    }

    void protocolReactiveAllowed()
    {
        Profile profile;
        profile.base.color = QColor("#102030");
        profile.reactive = {true, QColor("#A0B0C0"), 0x0123};
        const BuildResult result = buildPackets(profile, Layout::gs76(), true);
        const QByteArray first = result.packets.first().data;
        const QByteArray fragment = first.mid(4, 12);
        QCOMPARE(fragment.toHex(' '), QByteArray("10 20 30 a0 b0 c0 23 01 00 08 00 04"));
    }

    void protocolSizes()
    {
        for (const Profile &profile : {builtinProfile("ares"), Profile()})
            for (const Packet &packet : buildPackets(profile, Layout::gs76()).packets)
                QCOMPARE(packet.data.size(), packet.kind == Packet::Feature && quint8(packet.data.at(0)) != 0x09 ? 524
                                             : packet.kind == Packet::Output ? 64 : 64);
    }

    void protocolLegacy()
    {
        QVERIFY(profileFromLegacy("ares", QString()));
        const auto rgb = profileFromLegacy(QString(), "#00e5ff");
        QVERIFY(rgb);
        QCOMPARE(rgb->base.color, QColor("#00E5FF"));
        QVERIFY(!profileFromLegacy("nope", QString()));
    }

    void themeProfileOrder()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tron");
        const QByteArray klc = "{\"version\":1,\"base\":{\"type\":\"static\",\"color\":\"#112233\"}}";
        write(paths.stateTheme + "/keyboard.rgb", "#445566\n");
        QCOMPARE(resolveProfile(paths).profile->base.color, QColor("#445566"));
        write(paths.stateTheme + "/keyboard.effect", "tron\n");
        QCOMPARE(resolveProfile(paths).profile->effects.size(), 6);
        write(paths.stateTheme + "/keyboard.klc.json", klc);
        QCOMPARE(resolveProfile(paths).profile->base.color, QColor("#112233"));
        QDir().mkpath(paths.userThemes + "/tron");
        write(paths.userThemes + "/tron/keyboard.klc.json", QByteArray(klc).replace("112233", "778899"));
        const Resolved resolved = resolveProfile(paths);
        QCOMPARE(resolved.profile->base.color, QColor("#778899"));
        QCOMPARE(resolved.source, paths.userThemes + "/tron/keyboard.klc.json");
        QCOMPARE(resolved.savePath, paths.userThemes + "/tron/keyboard.klc.json");
        QCOMPARE(resolved.themeName, QString("tron"));
    }

    void themeProfileNothing()
    {
        QTemporaryDir dir;
        const Resolved resolved = resolveProfile(fakeTheme(dir, "tokyo-night"));
        QVERIFY(!resolved.profile);
        QVERIFY(resolved.errors.isEmpty());
        QVERIFY(resolved.source.isEmpty());
    }

    void themeProfileNoOmarchy()
    {
        QTemporaryDir dir;
        ThemePaths paths = fakeTheme(dir, "x");
        QFile::remove(paths.themeName);
        const Resolved resolved = resolveProfile(paths);
        QVERIFY(!resolved.profile);
        QVERIFY(resolved.errors.join(" ").contains("theme.name"));
    }

    void themeProfileInvalid()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tron");
        write(paths.stateTheme + "/keyboard.klc.json", "{\"version\":1,\"base\":{\"type\":\"static\",\"color\":\"#112233\"},\"keys\":{\"KEY_NOPE\":\"#FF0000\"}}");
        const Resolved resolved = resolveProfile(paths);
        QVERIFY(!resolved.profile);
        QVERIFY(resolved.errors.join(" ").contains("KEY_NOPE"));
    }

    void themeProfileSaveStock()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tokyo-night");
        const Resolved resolved = resolveProfile(paths);
        Profile profile;
        profile.base.color = QColor("#00E5FF");
        QString error;
        QVERIFY2(saveProfile(profile, resolved.savePath, &error), qPrintable(error));
        QCOMPARE(QDir(paths.userThemes + "/tokyo-night").entryList(QDir::Files | QDir::Hidden), QStringList{"keyboard.klc.json"});
    }

    void themeProfileAtomic()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + "/t/keyboard.klc.json";
        Profile profile;
        QString error;
        QVERIFY(saveProfile(profile, path, &error));
        profile.base.color = QColor("#ABCDEF");
        QVERIFY(saveProfile(profile, path, &error));
        QCOMPARE(QDir(dir.path() + "/t").entryList(QDir::Files | QDir::Hidden), QStringList{"keyboard.klc.json"});
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("#ABCDEF"));
    }

    void deviceOrder()
    {
        QTemporaryDir dir;
        FakeLog log;
        FakeTransport transport(log);
        FakeBinder binder(log);
        Device device = fakeDevice(transport, binder, dir, log);
        const ApplyResult result = device.apply(buildPackets(builtinProfile("tron"), Layout::gs76()).packets);
        QCOMPARE(int(result.error), int(ApplyError::None));
        QStringList expected{"bind", "open"};
        for (int i = 0; i < 12; ++i)
            expected << "send";
        expected << "close" << "unbind";
        QCOMPARE(log.events, expected);
    }

    void deviceUnbindOnSendFailure()
    {
        QTemporaryDir dir;
        FakeLog log;
        FakeTransport transport(log);
        transport.failAt = 3;
        FakeBinder binder(log);
        Device device = fakeDevice(transport, binder, dir, log);
        const ApplyResult result = device.apply(buildPackets(builtinProfile("tron"), Layout::gs76()).packets);
        QCOMPARE(int(result.error), int(ApplyError::SendFailed));
        QCOMPARE(log.events.count("send"), 3);
        QCOMPARE(log.events.last(), QString("unbind"));
    }

    void deviceMissing()
    {
        QTemporaryDir dir;
        FakeLog log;
        FakeTransport transport(log);
        transport.openOk = false;
        FakeBinder binder(log);
        Device device = fakeDevice(transport, binder, dir, log);
        const ApplyResult result = device.apply(buildPackets(Profile(), Layout::gs76()).packets);
        QCOMPARE(int(result.error), int(ApplyError::DeviceMissing));
        QVERIFY(result.message.contains("-71"));
        QCOMPARE(log.events.count("send"), 0);
        QCOMPARE(log.events.last(), QString("unbind"));
    }

    void deviceWaitsInterval()
    {
        QTemporaryDir dir;
        FakeLog log;
        FakeTransport transport(log);
        FakeBinder binder(log);
        Device device = fakeDevice(transport, binder, dir, log);
        const QList<Packet> packets = buildPackets(Profile(), Layout::gs76()).packets;
        QCOMPARE(int(device.apply(packets).error), int(ApplyError::None));
        QVERIFY(!log.sleeps.contains(4000));
        log.now = log.now + 1000 - qint64(log.sleeps.size()) * Device::PacketGapMs;
        log.sleeps.clear();
        QCOMPARE(int(device.apply(packets).error), int(ApplyError::None));
        QCOMPARE(log.sleeps.first(), 4000);
        QCOMPARE(log.events.count("bind"), 2);
    }

    void deviceSudoFastFail()
    {
        QTemporaryDir dir;
        FakeLog log;
        FakeTransport transport(log);
        FakeBinder binder(log);
        binder.bindOk = false;
        Device device = fakeDevice(transport, binder, dir, log);
        const ApplyResult result = device.apply(buildPackets(Profile(), Layout::gs76()).packets);
        QCOMPARE(int(result.error), int(ApplyError::BindFailed));
        QVERIFY(!log.events.contains("open"));
        QVERIFY(log.sleeps.isEmpty());
        // A timed-out bind may still complete as root: detach anyway.
        QCOMPARE(log.events, QStringList({"bind", "unbind"}));

        QElapsedTimer timer;
        timer.start();
        SudoBinder missing("/nonexistent/msi-kbd-usb");
        QString error;
        QVERIFY(!missing.bind(&error));
        QVERIFY(error.contains("msi-kbd-usb"));
        QVERIFY(timer.elapsed() < 1000);
    }


    void cliCheck()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tron");
        write(paths.stateTheme + "/keyboard.effect", "tron\n");
        CliRun run(dir, paths);
        QCOMPARE(run.exec({"apply", "--check"}), 0);
        QVERIFY2(run.out.contains("12 packets OK"), qPrintable(run.out));
        QVERIFY(run.log.events.isEmpty());
    }

    void cliInvalid()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tron");
        const QString profile = dir.path() + "/bad.json";
        write(profile, "{\"version\":1,\"base\":{\"type\":\"static\",\"color\":\"#000000\"},\"keys\":{\"KEY_NOPE\":\"#FF0000\"}}");
        CliRun run(dir, paths);
        QCOMPARE(run.exec({"apply", profile}), 1);
        QVERIFY2(run.err.contains("KEY_NOPE"), qPrintable(run.err));
        QVERIFY(run.log.events.isEmpty());
    }

    void cliNothing()
    {
        QTemporaryDir dir;
        CliRun run(dir, fakeTheme(dir, "tokyo-night"));
        QCOMPARE(run.exec({"apply"}), 0);
        QVERIFY(run.log.events.isEmpty());
    }

    void cliSends()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tokyo-night");
        write(paths.stateTheme + "/keyboard.rgb", "#7aa2f7\n");
        CliRun run(dir, paths);
        QCOMPARE(run.exec({"apply"}), 0);
        QCOMPARE(run.log.events.count("send"), 5);
    }

    void cliJson()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tron");
        write(paths.stateTheme + "/keyboard.effect", "tron\n");
        CliRun run(dir, paths);
        QCOMPARE(run.exec({"apply", "--check", "--json"}), 0);
        const QJsonObject result = QJsonDocument::fromJson(run.out.toUtf8()).object();
        QCOMPARE(result.value("ok").toBool(), true);
        QCOMPARE(result.value("packets").toInt(), 12);
        QCOMPARE(result.value("source").toString(), paths.stateTheme + "/keyboard.effect");
    }


    void previewStatic()
    {
        Profile profile;
        profile.base.color = QColor("#00E5FF");
        profile.keys.insert("KEY_Q", QColor("#FF0000"));
        const Preview preview(profile, Layout::gs76());
        QCOMPARE(preview.colorAt("KEY_Q", 0), QColor("#FF0000"));
        QCOMPARE(preview.colorAt("KEY_Q", 12345), QColor("#FF0000"));
        QCOMPARE(preview.colorAt("KEY_W", 777), QColor("#00E5FF"));
    }

    void previewCycle()
    {
        Profile profile;
        EffectLayer pulse;
        pulse.type = "pulse";
        pulse.keys = {"KEY_Q"};
        pulse.start = QColor("#500000");
        pulse.steps = {{QColor("#FF1010"), 60}};
        pulse.closing = 60;
        pulse.origin = "aqua-0";
        profile.effects = {pulse};
        const Preview preview(profile, Layout::gs76());
        QCOMPARE(preview.colorAt("KEY_Q", 0), QColor("#500000"));
        const QColor half = preview.colorAt("KEY_Q", 30 * Preview::UnitMs);
        QVERIFY(qAbs(half.red() - (0x50 + 0xFF) / 2) <= 1);
        QCOMPARE(preview.colorAt("KEY_Q", 60 * Preview::UnitMs), QColor("#FF1010"));
        QCOMPARE(preview.colorAt("KEY_Q", 120 * Preview::UnitMs), QColor("#500000"));
    }

    void previewWaveDelay()
    {
        const Layout &layout = Layout::gs76();
        Profile profile;
        EffectLayer wave;
        wave.type = "wave";
        wave.keys = {"KEY_Q", "KEY_P"};
        wave.start = Qt::black;
        wave.steps = {{Qt::white, 50}};
        wave.origin = "aqua-1";
        profile.effects = {wave};
        const Preview preview(profile, layout);
        const qint64 delay = qint64((layout.byId("KEY_P")->x - layout.byId("KEY_Q")->x) * Preview::WaveMsPerUnit);
        QVERIFY(delay > 0);
        for (qint64 t : {100, 260, 730})
            QCOMPARE(preview.colorAt("KEY_P", t + delay), preview.colorAt("KEY_Q", t));
    }


    void backendSelectGroup()
    {
        EditorRun run;
        run.backend->selectGroup("wasd");
        QCOMPARE(run.backend->selection(), QStringList({"KEY_W", "KEY_A", "KEY_S", "KEY_D"}));
        run.backend->select({"KEY_Q"}, true);
        QCOMPARE(run.backend->selection().size(), 5);
        run.backend->select({"KEY_Q"}, false);
        QCOMPARE(run.backend->selection(), QStringList{"KEY_Q"});
    }

    void backendPaint()
    {
        EditorRun run;
        QVERIFY(!run.backend->dirty());
        run.backend->setCurrentLayer(1);
        run.backend->select({"KEY_Q"}, false);
        run.backend->setColor(QColor("#FF0000"));
        QCOMPARE(run.backend->profile().keys.value("KEY_Q"), QColor("#FF0000"));
        QVERIFY(run.backend->dirty());
        QCOMPARE(run.backend->previewColor("KEY_Q", 0), QColor("#FF0000"));
    }

    void backendApplySaves()
    {
        EditorRun run;
        run.backend->setCurrentLayer(0);
        run.backend->setColor(QColor("#123456"));
        run.backend->apply();
        QTRY_COMPARE(run.sent, 1);
        QTRY_VERIFY(!run.backend->busy());
        QVERIFY(!run.backend->dirty());
        QVERIFY(run.backend->error().isEmpty());
        QFile file(run.paths.userThemes + "/tokyo-night/keyboard.klc.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("#123456"));
        QVERIFY(run.backend->cooldownMs() > 0);
    }

    void backendApplyInvalid()
    {
        EditorRun run;
        for (int i = 1; i <= 7; ++i) {
            run.backend->select({QString("KEY_F%1").arg(i)}, false);
            run.backend->addEffect("pulse");
        }
        run.backend->apply();
        QVERIFY(run.backend->error().contains("6"));
        QTest::qWait(50);
        QCOMPARE(run.sent, 0);
        QVERIFY(!QFile::exists(run.paths.userThemes + "/tokyo-night/keyboard.klc.json"));
    }

    void backendUndo()
    {
        EditorRun run;
        run.backend->setCurrentLayer(1);
        run.backend->select({"KEY_Q"}, false);
        run.backend->setColor(QColor("#FF0000"));
        run.backend->setColor(QColor("#00FF00"));
        run.backend->undo();
        QCOMPARE(run.backend->profile().keys.value("KEY_Q"), QColor("#FF0000"));
        run.backend->undo();
        QVERIFY(!run.backend->profile().keys.contains("KEY_Q"));
        run.backend->redo();
        QCOMPARE(run.backend->profile().keys.value("KEY_Q"), QColor("#FF0000"));
    }

    void backendEffectLayer()
    {
        EditorRun run;
        run.backend->selectGroup("function");
        run.backend->addEffect("wave");
        QCOMPARE(run.backend->profile().effects.size(), 1);
        QCOMPARE(run.backend->profile().effects.first().keys.size(), 12);
        QCOMPARE(run.backend->currentLayer(), 2);
        run.backend->setStepColor(0, QColor("#ABCDEF"));
        QCOMPARE(run.backend->profile().effects.first().steps.first().color, QColor("#ABCDEF"));
        run.backend->removeLayer(2);
        QVERIFY(run.backend->profile().effects.isEmpty());
    }


    void backendKeysDoNotOverlap()
    {
        EditorRun run;
        const QVariantList keys = run.backend->keys();
        QCOMPARE(keys.size(), 101);
        for (const QVariant &key : keys)
            QVERIFY(key.toMap()["right"].toDouble() > key.toMap()["left"].toDouble());
        for (int i = 0; i < keys.size(); ++i)
            for (int j = i + 1; j < keys.size(); ++j) {
                const QVariantMap a = keys[i].toMap(), b = keys[j].toMap();
                if (a["y"].toDouble() != b["y"].toDouble())
                    continue;
                const bool overlap = a["left"].toDouble() < b["right"].toDouble() && b["left"].toDouble() < a["right"].toDouble();
                QVERIFY2(!overlap, qPrintable(a["id"].toString() + " overlaps " + b["id"].toString()));
            }
    }


    void frenchCatalogCoversDeviceMessages()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "fr\n");
        Translator translator(dir.path() + "/language", ":/i18n");
        QCOMPARE(translator.translate("Device", "Could not attach the keyboard: %1"), QString("Impossible d'attacher le clavier : %1"));
        QVERIFY(!translator.translate("Device", "The keyboard did not appear after attaching it (USB error -71 is likely). "
                                                "Power-cycle its USB port or shut the computer down completely.").isNull());
        QCOMPARE(translator.translate("Main", "%n key(s) selected", nullptr, 3), QString("%n touches sélectionnées"));
    }


    void devicePausesBetweenPackets()
    {
        QTemporaryDir dir;
        FakeLog log;
        FakeTransport transport(log);
        FakeBinder binder(log);
        Device device = fakeDevice(transport, binder, dir, log);
        QCOMPARE(int(device.apply(buildPackets(builtinProfile("tron"), Layout::gs76()).packets).error), int(ApplyError::None));
        QCOMPARE(log.sleeps, QList<int>(12, Device::PacketGapMs));
        QCOMPARE(Device::PacketGapMs, 10);
    }

    void effectsFullRangeBaseEffects_data()
    {
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("color");
        QTest::newRow("rainbow") << "rainbow" << "#FF0000";
        QTest::newRow("breathing white") << "breathing" << "#FFFFFF";
        QTest::newRow("flashing red") << "flashing" << "#FF0000";
    }

    void effectsFullRangeBaseEffects()
    {
        QFETCH(QString, type);
        QFETCH(QString, color);
        Profile profile;
        profile.base.type = type;
        profile.base.color = QColor(color);
        const BuildResult result = buildPackets(profile, Layout::gs76());
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join("; ")));
        QString error;
        int sum[3] = {0, 0, 0};
        for (const Transition &t : transitions(baseEffect(profile.base), &error)) {
            QVERIFY(qAbs(t.dr) <= 127 && qAbs(t.dg) <= 127 && qAbs(t.db) <= 127);
            sum[0] += t.dr; sum[1] += t.dg; sum[2] += t.db;
        }
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(sum[0], 0);
        QCOMPARE(sum[1], 0);
        QCOMPARE(sum[2], 0);
    }

    void effectsTotalDurationFits()
    {
        const EffectDef def{Qt::red, {{Qt::black, 40000}}, 40000, "aqua-1"};
        QString error;
        transitions(def, &error);
        QVERIFY2(error.contains("65535"), qPrintable(error));
    }

    void cliRapidThemeSwitch()
    {
        // Theme A is applied; theme B's apply starts within the interval, and while it
        // waits the theme switches to C. C must be what reaches the keyboard.
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "a");
        write(paths.stateTheme + "/keyboard.rgb", "#111111\n");
        CliRun run(dir, paths);
        QCOMPARE(run.exec({"apply"}), 0);
        write(paths.themeName, "b\n");
        write(paths.stateTheme + "/keyboard.rgb", "#222222\n");
        run.log.now += 1000;
        run.onSleep = [&](int ms) {
            if (ms > Device::PacketGapMs) {
                write(paths.themeName, "c\n");
                write(paths.stateTheme + "/keyboard.rgb", "#333333\n");
            }
        };
        run.log.packets.clear();
        QCOMPARE(run.exec({"apply"}), 0);
        QVERIFY(!run.log.packets.isEmpty());
        QCOMPARE(run.log.packets.first().mid(4, 3), QByteArray::fromHex("333333"));
    }


    void uiBaseTypeChoicesVisible()
    {
        Ui ui;
        auto *box = ui.window->findChild<QQuickItem *>("baseTypeBox");
        QVERIFY(box && box->isVisible());
        QCOMPARE(box->property("count").toInt(), 5);
        QObject *popup = box->property("popup").value<QObject *>();
        QMetaObject::invokeMethod(popup, "open");
        QTRY_VERIFY(popup->property("opened").toBool());
        QTest::qWait(300);
        // The list must be drawn on an opaque background, not over the panel behind it.
        auto *background = popup->property("background").value<QQuickItem *>();
        const QPointF centre = background->mapToScene(QPointF(background->width() / 2, background->height() - 6));
        const QImage image = ui.window->grabWindow();
        image.save(qEnvironmentVariable("KLC_UI_SHOT", "/dev/null"), "PNG");
        const QColor pixel = image.pixelColor(centre.toPoint() * image.devicePixelRatio());
        QCOMPARE(pixel, background->property("color").value<QColor>());
    }

    void uiQuitDiscardCloses()
    {
        Ui ui;
        ui.run.backend->setColor(QColor("#123456"));
        QVERIFY(ui.run.backend->dirty());
        QMetaObject::invokeMethod(ui.window, "requestQuit");
        auto *dialog = ui.window->findChild<QObject *>("quitDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QMetaObject::invokeMethod(dialog, "discarded");
        QTRY_VERIFY(!ui.window->isVisible());
        // The bug reopened the dialog from the window's closing handler, forever.
        QTest::qWait(300);
        QVERIFY(!dialog->property("opened").toBool());
        QVERIFY(!ui.window->isVisible());
    }


    // Re-picking the current value is not an edit: it must not fill the undo history.
    void backendNoOpEditKeepsHistory()
    {
        EditorRun run;
        run.backend->setColor(QColor("#123456"));
        run.backend->setColor(QColor("#123456"));
        run.backend->undo();
        QVERIFY(!run.backend->canUndo());
    }

    // A whole gesture (press, drag, release) is one undo step, however many values it went through.
    void backendGestureIsOneUndoStep()
    {
        EditorRun run;
        run.backend->beginGesture();
        run.backend->setColor(QColor("#110000"));
        run.backend->setColor(QColor("#220000"));
        run.backend->setColor(QColor("#330000"));
        run.backend->endGesture();
        QVERIFY(run.backend->canUndo());
        run.backend->undo();
        QVERIFY(!run.backend->canUndo());
        // After the gesture, edits are separate steps again.
        run.backend->setColor(QColor("#440000"));
        run.backend->setColor(QColor("#550000"));
        run.backend->undo();
        QVERIFY(run.backend->canUndo());
    }

    // Point of the hue ring at `degrees` (counter-clockwise from the right), in scene coordinates.
    static QPointF ringPoint(QQuickItem *ring, double degrees)
    {
        const double a = qDegreesToRadians(degrees);
        return ring->mapToScene(QPointF(75 + 63 * std::cos(a), 75 - 63 * std::sin(a)));
    }

    static double hueDistance(const QColor &a, const QColor &b)
    {
        const double d = std::abs(a.hsvHueF() - b.hsvHueF());
        return std::min(d, 1.0 - d);
    }

    // The bug: the ring is drawn red, magenta, blue… counter-clockwise but read red, yellow,
    // green…, so clicking magenta picked yellow. The picked hue must be the clicked pixel's.
    void uiHueRingPicksTheClickedColor()
    {
        Ui ui;
        auto *ring = ui.window->findChild<QQuickItem *>("hueRing");
        QVERIFY(ring && ring->isVisible());
        QTest::qWait(200);
        for (const double degrees : {30.0, 90.0, 150.0, 210.0, 270.0, 330.0}) {
            const QPointF point = ringPoint(ring, degrees);
            const QImage image = ui.window->grabWindow();
            const QColor pixel = image.pixelColor((point * image.devicePixelRatio()).toPoint());
            QTest::mouseClick(ui.window, Qt::LeftButton, {}, point.toPoint());
            const QColor picked = ring->parentItem()->property("color").value<QColor>();
            QVERIFY2(hueDistance(pixel, picked) < 0.04,
                     qPrintable(QString("%1°: pixel %2, picked %3").arg(degrees).arg(pixel.name(), picked.name())));
        }
    }

    // From a black (or nearly black) color, a hue picked on the ring must show: full intensity.
    void uiHueRingFromBlackIsVisible()
    {
        Ui ui;
        auto *ring = ui.window->findChild<QQuickItem *>("hueRing");
        QVERIFY(ring && ring->isVisible());
        ring->parentItem()->setProperty("color", QColor("#000000"));
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, ringPoint(ring, 90).toPoint());
        QCOMPARE(ring->parentItem()->property("color").value<QColor>().valueF(), 1.0);
    }

    void uiRingDragIsOneUndoStep()
    {
        Ui ui;
        auto *ring = ui.window->findChild<QQuickItem *>("hueRing");
        QVERIFY(ring && ring->isVisible());
        QTest::mousePress(ui.window, Qt::LeftButton, {}, ringPoint(ring, 10).toPoint());
        for (int degrees = 20; degrees <= 120; degrees += 10)
            QTest::mouseMove(ui.window, ringPoint(ring, degrees).toPoint());
        QTest::mouseRelease(ui.window, Qt::LeftButton, {}, ringPoint(ring, 120).toPoint());
        QVERIFY(ui.run.backend->canUndo());
        ui.run.backend->undo();
        QVERIFY(!ui.run.backend->canUndo());
    }

    // Dragging a slider is one undo step too.
    void uiSliderDragIsOneUndoStep()
    {
        Ui ui;
        auto *slider = ui.window->findChild<QQuickItem *>("brightnessSlider");
        QVERIFY(slider && slider->isVisible());
        const QPointF left = slider->mapToScene(QPointF(slider->width() * 0.2, slider->height() / 2));
        QTest::mousePress(ui.window, Qt::LeftButton, {}, left.toPoint());
        for (double f = 0.25; f <= 0.6; f += 0.05)
            QTest::mouseMove(ui.window, slider->mapToScene(QPointF(slider->width() * f, slider->height() / 2)).toPoint());
        QTest::mouseRelease(ui.window, Qt::LeftButton, {},
                            slider->mapToScene(QPointF(slider->width() * 0.6, slider->height() / 2)).toPoint());
        QVERIFY(ui.run.backend->canUndo());
        ui.run.backend->undo();
        QVERIFY(!ui.run.backend->canUndo());
    }

    // In a half-screen window (960×527, common in Hyprland), Apply, Undo and Redo stay on screen.
    void uiActionBarVisibleInSmallWindow()
    {
        Ui ui;
        ui.window->resize(960, 527);
        QTest::qWait(200);
        const QRectF bounds(0, 0, ui.window->width(), ui.window->height());
        for (const char *name : {"applyButton", "undoButton", "redoButton"}) {
            auto *button = ui.window->findChild<QQuickItem *>(name);
            QVERIFY2(button && button->isVisible(), name);
            const QRectF r = button->mapRectToScene(QRectF(0, 0, button->width(), button->height()));
            QVERIFY2(bounds.contains(r), qPrintable(QString("%1 at %2,%3 %4×%5 outside the window")
                                                         .arg(name).arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height())));
        }
    }

    // Undo must be visible, not only Ctrl+Z.
    void uiUndoRedoButtons()
    {
        Ui ui;
        auto *undo = ui.window->findChild<QQuickItem *>("undoButton");
        auto *redo = ui.window->findChild<QQuickItem *>("redoButton");
        QVERIFY(undo && undo->isVisible() && redo && redo->isVisible());
        QVERIFY(!undo->isEnabled());
        ui.run.backend->setColor(QColor("#123456"));
        QTRY_VERIFY(undo->isEnabled());
        QMetaObject::invokeMethod(undo, "clicked");
        QVERIFY(!ui.run.backend->canUndo());
        QTRY_VERIFY(redo->isEnabled());
    }

    void deviceKeyboardAbsent()
    {
        QTemporaryDir dir;
        FakeLog log;
        FakeTransport transport(log);
        FakeBinder binder(log);
        binder.bindOk = false;
        binder.bindError = "SteelSeries KLC (1038:113a) not found";
        Device device = fakeDevice(transport, binder, dir, log);
        const ApplyResult result = device.apply(buildPackets(Profile(), Layout::gs76()).packets);
        QCOMPARE(int(result.error), int(ApplyError::KeyboardAbsent));
        QVERIFY(result.message.contains("-71"));
        QVERIFY(result.message.contains("Shut the computer down completely"));
        QVERIFY(!log.events.contains("open"));
    }

    void cliKeyboardAbsentExitCode()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tokyo-night");
        write(paths.stateTheme + "/keyboard.rgb", "#7aa2f7\n");
        CliRun run(dir, paths);
        run.binder.bindOk = false;
        run.binder.bindError = "SteelSeries KLC (1038:113a) not found";
        QCOMPARE(run.exec({"apply"}), 2);
        QVERIFY(run.err.contains("-71"));
    }


    void backendBaseEffectShowsOnWholeKeyboard()
    {
        // With the tron theme's ares profile, effect layers cover almost every key, so a
        // base effect has to replace them to be seen at all, as in Mystic Light.
        EditorRun run("ares");
        QCOMPARE(run.backend->profile().effects.size(), 6);
        run.backend->setCurrentLayer(0);
        run.backend->setBaseType("rainbow");
        QVERIFY(run.backend->profile().effects.isEmpty());
        QVERIFY(run.backend->notice().contains("6"));
        QVERIFY(run.backend->previewColor("KEY_Q", 0) != run.backend->previewColor("KEY_Q", 900));
        run.backend->undo();
        QCOMPARE(run.backend->profile().effects.size(), 6);
        QCOMPARE(run.backend->profile().base.type, QString("static"));
    }


    void layoutMatchesPhoto()
    {
        // Measured on a top-down photo of the GS76 keyboard (pixels, y downwards).
        const Layout &layout = Layout::gs76();
        const Key *backslash = layout.byId("KEY_BACKSLASH");
        QVERIFY(backslash && backslash->visible && backslash->hid == 49);
        QVERIFY(qAbs(backslash->x - 760) <= 2 && qAbs(backslash->y - 300) <= 2);
        const Key *iso = layout.byId("KEY_102ND");
        QVERIFY(iso && iso->visible && iso->hid == 100);
        const Key *altgr = layout.byId("KEY_RIGHTALT"), *fn = layout.byId("KEY_FN");
        QVERIFY(iso->x > altgr->x && iso->x < fn->x);
        QCOMPARE(iso->y, altgr->y);
        const Key *space = layout.byId("KEY_SPACE");
        QVERIFY(space->w > 5 * layout.byId("KEY_Q")->w);
        const Key *plus = layout.byId("KEY_KPPLUS");
        QVERIFY(plus->h > 2 * layout.byId("KEY_KP9")->h);
        QVERIFY(!layout.byHid(50) || !layout.byHid(50)->visible);
    }


    void layoutIdsUnique()
    {
        QSet<QString> ids;
        for (const Key &key : Layout::gs76().allKeys()) {
            QVERIFY2(!ids.contains(key.id), qPrintable(key.id));
            ids.insert(key.id);
            QCOMPARE(Layout::gs76().byId(key.id)->hid, key.hid);
        }
    }

private:


    // An editor on a fake theme whose sends are counted instead of reaching a keyboard.
    struct EditorRun {
        explicit EditorRun(const QByteArray &effect = {}) {
            paths.stateTheme = dir.path() + "/state/current/theme";
            paths.themeName = dir.path() + "/state/current/theme.name";
            paths.userThemes = dir.path() + "/config/themes";
            QDir().mkpath(paths.stateTheme);
            QFile name(paths.themeName);
            name.open(QIODevice::WriteOnly);
            name.write("tokyo-night\n");
            name.close();
            if (!effect.isEmpty()) {
                QFile file(paths.stateTheme + "/keyboard.effect");
                file.open(QIODevice::WriteOnly);
                file.write(effect + "\n");
            }
            backend = new Backend(paths, KeyLabels::fromRmlvo("fr", ""),
                                  [this](const QList<Packet> &) { ++sent; return ApplyResult{}; },
                                  dir.path() + "/colors.json");
        }
        ~EditorRun() { delete backend; }
        QTemporaryDir dir;
        ThemePaths paths;
        std::atomic<int> sent{0};
        Backend *backend = nullptr;
    };

    // Runs the command line against a fake theme and a fake keyboard.
    struct CliRun {
        CliRun(const QTemporaryDir &dir, const ThemePaths &paths)
            : transport(log), binder(log),
              device(transport, binder, dir.path(),
                     [this](int ms) { log.sleeps << ms; log.now += ms; if (onSleep) onSleep(ms); },
                     [this] { return log.now; }),
              outStream(&out), errStream(&err), env{paths, &device, &outStream, &errStream} {}
        int exec(const QStringList &args) {
            const int code = runCli(args, env);
            outStream.flush();
            errStream.flush();
            return code;
        }
        FakeLog log;
        std::function<void(int)> onSleep;
        FakeTransport transport;
        FakeBinder binder;
        Device device;
        QString out, err;
        QTextStream outStream, errStream;
        CliEnv env;
    };
    static Device fakeDevice(Transport &transport, Binder &binder, const QTemporaryDir &dir, FakeLog &log)
    {
        return Device(transport, binder, dir.path(), [&log](int ms) { log.sleeps << ms; log.now += ms; },
                      [&log] { return log.now; });
    }

    static ThemePaths fakeTheme(const QTemporaryDir &dir, const QString &name)
    {
        ThemePaths paths;
        paths.stateTheme = dir.path() + "/state/current/theme";
        paths.themeName = dir.path() + "/state/current/theme.name";
        paths.userThemes = dir.path() + "/config/themes";
        QDir().mkpath(paths.stateTheme);
        write(paths.themeName, name.toUtf8() + "\n");
        return paths;
    }

    static void compareWithFixture(const BuildResult &result, const QString &name)
    {
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join("; ")));
        const auto expected = fixture(name);
        QCOMPARE(result.packets.size(), expected.size());
        for (int i = 0; i < expected.size(); ++i) {
            QCOMPARE(QChar(result.packets[i].kind == Packet::Feature ? 'F' : 'O'), expected[i].first);
            if (result.packets[i].data != expected[i].second) {
                const QByteArray got = result.packets[i].data, want = expected[i].second;
                int at = 0;
                while (at < qMin(got.size(), want.size()) && got[at] == want[at])
                    ++at;
                QFAIL(qPrintable(QString("packet %1 differs at byte 0x%2: got %3, want %4")
                                     .arg(i).arg(at, 0, 16)
                                     .arg(QString(got.mid(at, 12).toHex(' ')), QString(want.mid(at, 12).toHex(' ')))));
            }
        }
    }

    // Reads a fixture written by tools/gen_fixtures.py: one "F <hex>" or "O <hex>" per packet.
    static QList<QPair<QChar, QByteArray>> fixture(const QString &name)
    {
        QList<QPair<QChar, QByteArray>> packets;
        QFile file(QString(KLC_FIXTURES) + "/" + name);
        if (!file.open(QIODevice::ReadOnly))
            qFatal("missing fixture %s", qPrintable(name));
        for (const QByteArray &line : file.readAll().split('\n'))
            if (line.size() > 2)
                packets.append({QChar(line.at(0)), QByteArray::fromHex(line.mid(2))});
        return packets;
    }

    // The transition table of an effect packet, as the controller stores it.
    static QList<Transition> table(const QByteArray &packet)
    {
        QList<Transition> result;
        const int count = quint8(packet.at(0x96));
        for (int i = 0; i < count; ++i) {
            const char *e = packet.constData() + 2 + i * 8;
            result.append({qint8(e[2]), qint8(e[3]), qint8(e[4]),
                           quint16(quint8(e[6]) | (quint8(e[7]) << 8))});
        }
        return result;
    }

    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

    // The real window, offscreen, on an editor whose sends are counted.
    struct Ui {
        Ui() : theme(run.dir.path()), translator(langDir.path() + "/language", ":/i18n") {
            static const bool styled = [] { QQuickStyle::setStyle("Material"); return true; }();
            Q_UNUSED(styled);
            engine.rootContext()->setContextProperty("translator", &translator);
            engine.rootContext()->setContextProperty("theme", &theme);
            engine.rootContext()->setContextProperty("backend", run.backend);
            engine.load(QUrl("qrc:/Main.qml"));
            window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if (!window || !QTest::qWaitForWindowExposed(window))
                qFatal("window did not open");
        }
        EditorRun run;
        QTemporaryDir langDir;
        Theme theme;
        Translator translator;
        QQmlApplicationEngine engine;
        QQuickWindow *window = nullptr;
    };
};

QTEST_MAIN(KlcLightTests)
#include "klc-light_tests.moc"
