#include <functional>
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
#include "document.h"
#include "sudobinder.h"
#include "effects.h"
#include "keylabels.h"
#include "layout.h"
#include "library.h"
#include "preview.h"
#include "profile.h"
#include "protocol.h"
#include "themeprofile.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
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

Q_DECLARE_METATYPE(Profile)

class KlcLightTests : public QObject {
    Q_OBJECT

private slots:
    // The tests read English strings: without an Omarchy language file the app follows the
    // system locale, so pin it to C whatever the developer's own locale is.
    void initTestCase()
    {
        qputenv("LC_ALL", "C");
    }

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

    // No Omarchy language picked: the system locale decides (LC_ALL, then LC_MESSAGES, then LANG).
    void translatorFollowsTheSystemLocale_data()
    {
        QTest::addColumn<QByteArray>("lcAll");
        QTest::addColumn<QByteArray>("lcMessages");
        QTest::addColumn<QByteArray>("lang");
        QTest::addColumn<QString>("expected");
        QTest::newRow("LANG fr") << QByteArray() << QByteArray() << QByteArray("fr_FR.UTF-8") << "fr";
        QTest::newRow("LC_MESSAGES wins") << QByteArray() << QByteArray("fr_FR.UTF-8") << QByteArray("en_US.UTF-8") << "fr";
        QTest::newRow("LC_ALL wins") << QByteArray("en_GB.UTF-8") << QByteArray("fr_FR.UTF-8") << QByteArray("fr_FR.UTF-8") << "en";
        QTest::newRow("C is English") << QByteArray("C") << QByteArray() << QByteArray("fr_FR.UTF-8") << "en";
        QTest::newRow("POSIX is English") << QByteArray("POSIX") << QByteArray() << QByteArray() << "en";
        QTest::newRow("nothing set") << QByteArray() << QByteArray() << QByteArray() << "en";
    }

    void translatorFollowsTheSystemLocale()
    {
        QFETCH(QByteArray, lcAll);
        QFETCH(QByteArray, lcMessages);
        QFETCH(QByteArray, lang);
        QFETCH(QString, expected);
        const QByteArray saved = qgetenv("LC_ALL"), savedMessages = qgetenv("LC_MESSAGES"), savedLang = qgetenv("LANG");
        auto set = [](const char *name, const QByteArray &value) {
            if (value.isEmpty())
                qunsetenv(name);
            else
                qputenv(name, value);
        };
        set("LC_ALL", lcAll);
        set("LC_MESSAGES", lcMessages);
        set("LANG", lang);
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", ":/i18n");
        set("LC_ALL", saved);
        set("LC_MESSAGES", savedMessages);
        set("LANG", savedLang);
        QCOMPARE(translator.language(), expected);
        if (expected == "fr")
            QCOMPARE(translator.translate("Main", "Save"), QString("Enregistrer"));
    }

    // A language picked in Omarchy wins over the system locale.
    void translatorOmarchyLanguageWinsOverSystem()
    {
        const QByteArray saved = qgetenv("LC_ALL");
        qputenv("LC_ALL", "fr_FR.UTF-8");
        QTemporaryDir dir;
        write(dir.path() + "/language", "en\n");
        Translator translator(dir.path() + "/language", ":/i18n");
        qputenv("LC_ALL", saved);
        QCOMPARE(translator.language(), QString("en"));
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
        QCOMPARE(layout.keys().size(), 102);
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
        // The power key is in none of the captured packets: it is sent as an extra element.
        for (const Key &key : layout.keys())
            QVERIFY2(aqua.contains(key.hid) || key.id == "KEY_POWER", qPrintable(key.id));
        QCOMPARE(int(layout.byId("KEY_Q")->hid), 0x14);
        QCOMPARE(layout.byHid(0x14)->id, QString("KEY_Q"));
    }

    void layoutGroups()
    {
        const Layout &layout = Layout::gs76();
        QCOMPARE(layout.group("wasd"), QStringList({"KEY_W", "KEY_A", "KEY_S", "KEY_D"}));
        QCOMPARE(layout.group("function").size(), 12);
        QCOMPARE(layout.group("digits").size(), 10);
        QCOMPARE(layout.group("all").size(), 102);
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

    // SteelSeries Engine's own pulse (aqua-0) walks the hue wheel: slopes of 80/16 level per
    // unit over 51 units are full 255-level swings. Our encoder must give the same table.
    void effectsMatchesCapturedPulse()
    {
        QByteArray pulse;
        QFile file(QString(KLC_FIXTURES) + "/aqua.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        for (const QJsonValue &hex : QJsonDocument::fromJson(file.readAll()).array()) {
            const QByteArray packet = QByteArray::fromHex(hex.toString().toLatin1());
            if (quint8(packet.at(0)) == 0x0b && packet.at(2) == 0)
                pulse = packet;
        }
        QVERIFY(!pulse.isEmpty());
        const EffectDef def{QColor(255, 0, 0),
                            {{QColor(255, 255, 0), 51}, {QColor(0, 255, 0), 51}, {QColor(0, 255, 255), 51},
                             {QColor(0, 0, 255), 51}, {QColor(255, 0, 255), 51}},
                            45, "aqua-0"};
        QString error;
        QCOMPARE(transitions(def, &error), table(pulse));
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }

    // The user's Tron wave at speed 235: its red step lasts 17 units, far from 32.
    void effectsReachesStepColors()
    {
        const EffectDef def{QColor("#DFA100"), {{QColor("#DFA100"), 23}, {QColor("#FF000D"), 17}}, 23, "aqua-1"};
        QString error;
        const QList<Transition> list = transitions(def, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const QList<QColor> reached = simulate(def.start, list);
        QVERIFY2(reached.size() == 3, "a channel wraps");
        QVERIFY2(near(reached[1], QColor("#FF000D"), 2), qPrintable(reached[1].name()));
        QVERIFY2(near(reached[2], QColor("#DFA100"), 2), qPrintable(reached[2].name()));
    }

    // A slope holds at most 127/16 level per unit: a full swing needs 33 units, so a
    // shorter step is lengthened rather than left short.
    void effectsStretchesTooFastSteps()
    {
        const EffectDef def{QColor("#FFFFFF"), {{QColor("#FFFFFF"), 20}, {QColor("#000000"), 2}}, 2, "aqua-0"};
        QString error;
        const QList<Transition> list = transitions(def, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(list[1].duration, quint16(33));
        const QList<QColor> reached = simulate(def.start, list);
        QVERIFY2(reached.size() == 3, "a channel wraps");
        QVERIFY2(near(reached[1], Qt::black, 2), "black not reached");
    }

    // The controller wraps a channel that leaves 0..255, so no step may overshoot.
    void effectsNeverWraps()
    {
        const EffectDef def{QColor("#010101"), {{QColor("#FE00FF"), 10}, {QColor("#030303"), 10}}, 10, "aqua-1"};
        QString error;
        const QList<Transition> list = transitions(def, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!simulate(def.start, list).isEmpty());
        QVERIFY2(near(simulate(def.start, list).last(), def.start, 2), "cycle does not come back");
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

    void documentRoundTrip()
    {
        Document d;
        d.name = "Mine";
        d.brightness = 70;
        d.speed = 150;
        d.reactive = {true, QColor("#A0B0C0"), 40};
        Group stat;
        stat.name = "WA";
        stat.keys = {"KEY_W", "KEY_A"};
        stat.color = QColor("#00E5FF");
        Group pulse;
        pulse.name = "Pulse";
        pulse.behavior = "pulse";
        pulse.keys = {"KEY_F1"};
        pulse.start = QColor("#000000");
        pulse.steps = {{QColor("#FF0000"), 100}, {QColor("#00FF00"), 200}};
        pulse.closing = 50;
        pulse.origin = "aqua-0";
        Group wave;
        wave.name = "Wave";
        wave.behavior = "wave";
        wave.keys = {"KEY_Q"};
        wave.steps = {{QColor("#0000FF"), 300}};
        wave.start = wave.steps.first().color;
        wave.origin = "aqua-2";
        d.groups = {stat, pulse, wave};

        const QJsonObject json = toJson(d);
        QCOMPARE(json["version"].toInt(), 2);
        QCOMPARE(json["name"].toString(), d.name);
        QStringList errors;
        const auto back = documentFromJson(json, &errors);
        QVERIFY2(back && errors.isEmpty(), qPrintable(errors.join("; ")));
        QCOMPARE(back->name, d.name);
        QCOMPARE(back->brightness, 70);
        QCOMPARE(back->speed, 150);
        QCOMPARE(back->reactive.duration, 40);
        QCOMPARE(back->reactive.color, QColor("#A0B0C0"));
        QVERIFY(back->reactive.enabled);
        QCOMPARE(back->groups.size(), 3);
        for (int i = 0; i < 3; ++i) {
            const Group &a = d.groups[i], &b = back->groups[i];
            QCOMPARE(b.name, a.name);
            QCOMPARE(b.behavior, a.behavior);
            QCOMPARE(b.keys, a.keys);
            QCOMPARE(b.closing, a.closing);
            QCOMPARE(b.origin, a.origin);
            QCOMPARE(b.steps.size(), a.steps.size());
            if (a.animated())
                QCOMPARE(b.start, a.start);
            else
                QCOMPARE(b.color, a.color);
        }
        QCOMPARE(back->groups[1].steps[1].color, QColor("#00FF00"));
        QCOMPARE(back->groups[1].steps[1].duration, 200);
        QVERIFY(validate(*back, Layout::gs76()).isEmpty());
    }

    void documentFromV1()
    {
        Profile p;
        p.keys = {{"KEY_W", QColor("#FF0000")}, {"KEY_A", QColor("#FF0000")}, {"KEY_ESC", QColor("#00FF00")}};
        EffectLayer e;
        e.name = "Pulse 1";
        e.type = "pulse";
        e.keys = {"KEY_F1"};
        e.start = QColor("#112233");
        e.steps = {{QColor("#445566"), 77}};
        e.closing = 9;
        e.origin = "aqua-3";
        p.effects = {e};
        const Document d = fromLighting(p, "X");
        QCOMPARE(d.name, QString("X"));
        QCOMPARE(d.groups.size(), 3);
        // Layout order lists KEY_A and KEY_W before KEY_ESC, so red opens the first group.
        const Group &red = d.groups[0], &green = d.groups[1];
        QVERIFY(!red.animated());
        QCOMPARE(red.color.name(), QString("#ff0000"));
        QCOMPARE(red.name, QString("Group #ff0000"));
        QCOMPARE(green.name, QString("Group #00ff00"));
        QCOMPARE(red.keys.size(), 2);
        const Group &pulse = d.groups[2];
        QCOMPARE(pulse.name, QString("Pulse 1"));
        QCOMPARE(pulse.behavior, QString("pulse"));
        QCOMPARE(pulse.start, QColor("#112233"));
        QCOMPARE(pulse.steps.size(), 1);
        QCOMPARE(pulse.steps[0].duration, 77);
        QCOMPARE(pulse.closing, 9);
        QCOMPARE(pulse.origin, QString("aqua-3"));
        QCOMPARE(d.groupOf("KEY_F1"), 2);
        QCOMPARE(d.groupOf("KEY_Z"), -1);
    }

    void documentFromV1UniqueNames()
    {
        Profile p;
        auto effect = [](const QString &name, const QString &key) {
            EffectLayer e;
            e.name = name;
            e.type = "pulse";
            e.keys = {key};
            e.start = QColor("#000000");
            e.steps = {{QColor("#FFFFFF"), 10}};
            e.origin = "aqua-0";
            return e;
        };
        p.effects = {effect("Pulse 2", "KEY_F1"), effect("Pulse 2", "KEY_F2"), effect("", "KEY_F3")};
        const Document d = fromLighting(p, "X");
        QCOMPARE(d.groups.size(), 3);
        QSet<QString> names;
        for (const Group &g : d.groups) {
            QVERIFY(!g.name.isEmpty());
            names.insert(g.name);
        }
        QCOMPARE(names.size(), 3);
        QVERIFY(validate(d, Layout::gs76()).isEmpty());

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("v1.klc.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(toJson(p)).toJson());
        file.close();
        QStringList errors;
        QVERIFY2(readDocument(path, &errors), qPrintable(errors.join("; ")));
    }

    void documentV1EffectWinsOverPaint()
    {
        Profile p;
        p.keys = {{"KEY_F1", QColor("#FF0000")}, {"KEY_W", QColor("#FF0000")}};
        EffectLayer e;
        e.name = "E";
        e.type = "pulse";
        e.keys = {"KEY_F1"};
        e.start = QColor("#000000");
        e.steps = {{QColor("#FFFFFF"), 10}};
        e.origin = "aqua-0";
        p.effects = {e};
        const Document d = fromLighting(p, "X");
        QCOMPARE(d.groups.size(), 2);
        QVERIFY(!d.groups[0].keys.contains("KEY_F1"));
        QVERIFY(d.groups[1].keys.contains("KEY_F1"));
    }

    void documentSamePackets_data()
    {
        QTest::addColumn<Profile>("profile");
        Profile all;
        all.base.color = QColor("#00E5FF");
        Profile zqsd = all;
        for (const QString &id : {"KEY_Q", "KEY_W", "KEY_E", "KEY_A", "KEY_S", "KEY_D"})
            zqsd.keys.insert(id, QColor("#FF0000"));
        QTest::newRow("ares") << builtinProfile("ares");
        QTest::newRow("tron") << builtinProfile("tron");
        QTest::newRow("static") << all;
        QTest::newRow("zqsd") << zqsd;
    }

    void documentSamePackets()
    {
        QFETCH(Profile, profile);
        expectSamePackets(profile);
    }

    void documentTronUserFileSamePackets()
    {
        QStringList errors;
        const auto profile = readProfile(QString(KLC_FIXTURES) + "/tron-user.klc.json", &errors);
        QVERIFY2(profile, qPrintable(errors.join("; ")));
        expectSamePackets(*profile);
    }

    void documentRejects_data()
    {
        QTest::addColumn<QStringList>("names");
        QTest::addColumn<QString>("expected");
        QTest::newRow("empty name") << QStringList{"empty"} << "name";
        QTest::newRow("duplicate name") << QStringList{"dup"} << "name";
        QTest::newRow("key twice") << QStringList{"twice"} << "KEY_W";
        QTest::newRow("unknown key") << QStringList{"unknown"} << "KEY_NOPE";
        QTest::newRow("no steps") << QStringList{"nosteps"} << "step";
        QTest::newRow("seven animated") << QStringList{"seven"} << "animated";
    }

    void documentRejects()
    {
        QFETCH(QStringList, names);
        QFETCH(QString, expected);
        auto stat = [](const QString &name, const QStringList &keys) {
            Group g;
            g.name = name;
            g.keys = keys;
            return g;
        };
        auto pulse = [](const QString &name, const QStringList &keys) {
            Group g;
            g.name = name;
            g.behavior = "pulse";
            g.keys = keys;
            g.start = QColor("#000000");
            g.steps = {{QColor("#FFFFFF"), 10}};
            g.origin = "aqua-0";
            return g;
        };
        Document d;
        const QString kind = names.first();
        if (kind == "empty")
            d.groups = {stat("", {"KEY_W"})};
        else if (kind == "dup")
            d.groups = {stat("A", {"KEY_W"}), stat("A", {"KEY_Q"})};
        else if (kind == "twice")
            d.groups = {stat("A", {"KEY_W"}), stat("B", {"KEY_W"})};
        else if (kind == "unknown")
            d.groups = {stat("A", {"KEY_NOPE"})};
        else if (kind == "nosteps") {
            Group g = pulse("A", {"KEY_W"});
            g.steps.clear();
            d.groups = {g};
        } else {
            for (int i = 0; i < 7; ++i)
                d.groups.append(pulse(QString("P%1").arg(i), {QString("KEY_F%1").arg(i + 1)}));
        }
        const QStringList errors = validate(d, Layout::gs76());
        QVERIFY(!errors.isEmpty());
        QVERIFY2(errors.join("; ").contains(expected), qPrintable(errors.join("; ")));
    }

    void documentReadsV1File()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Profile p;
        p.base.color = QColor("#00E5FF");
        p.keys.insert("KEY_W", QColor("#FF0000"));
        const QString path = dir.filePath("v1.klc.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(toJson(p)).toJson());
        file.close();
        QStringList errors;
        const auto d = readDocument(path, &errors);
        QVERIFY2(d, qPrintable(errors.join("; ")));
        QVERIFY(d->name.isEmpty());
        QCOMPARE(d->groups.size(), 1);
        QVERIFY(validate(*d, Layout::gs76()).isEmpty());

        // And a v2 file survives a save and a read.
        Document copy = *d;
        copy.name = "Saved";
        QString error;
        QVERIFY2(saveDocument(copy, dir.filePath("sub/v2.klc.json"), &error), qPrintable(error));
        const auto back = readDocument(dir.filePath("sub/v2.klc.json"), &errors);
        QVERIFY2(back, qPrintable(errors.join("; ")));
        QCOMPARE(back->name, QString("Saved"));
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
        for (const QString id : {"KEY_Q", "KEY_W", "KEY_E", "KEY_A", "KEY_S", "KEY_D"})
            profile.keys.insert(id, QColor("#FF0000"));
        compareWithFixture(buildPackets(profile, Layout::gs76()), "static-zqsd.hex");
    }

    // msi-kbd-effect wrote these fixtures with slopes of two levels per transition, which the
    // keyboard showed wrong (Tron's red came out orange). Every other byte still has to match;
    // the transition tables are checked against SteelSeries' own capture instead.
    void protocolAres()
    {
        compareWithFixture(buildPackets(builtinProfile("ares"), Layout::gs76()), "ares.hex", SkipTransitions);
    }
    void protocolTron()
    {
        compareWithFixture(buildPackets(builtinProfile("tron"), Layout::gs76()), "tron.hex", SkipTransitions);
    }

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

    // The power key rides as a twelfth element of the 11-key packet, whose byte 2 counts them.
    static QByteArray powerPacket(const BuildResult &result)
    {
        for (const Packet &packet : result.packets)
            if (quint8(packet.data.at(0)) == 0x0e && quint8(packet.data.at(2)) == 0x0c)
                return packet.data;
        return {};
    }

    void protocolPowerStatic()
    {
        Profile profile;
        profile.base.color = QColor("#00E5FF");
        profile.keys.insert("KEY_POWER", QColor("#FF0000"));
        const BuildResult result = buildPackets(profile, Layout::gs76());
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join("; ")));
        const QByteArray packet = powerPacket(result);
        QVERIFY(!packet.isEmpty());
        QCOMPARE(packet.mid(4 + 11 * 12, 12).toHex(' '), QByteArray("ff 00 00 00 00 00 00 00 00 01 00 66"));
        QList<quint8> counts;
        for (const Packet &p : result.packets)
            if (quint8(p.data.at(0)) == 0x0e)
                counts.append(quint8(p.data.at(2)));
        QCOMPARE(counts, QList<quint8>({0x2a, 0x0c, 0x18, 0x24}));
    }

    void protocolPowerAnimated()
    {
        Profile profile;
        profile.base.type = "wave";
        profile.base.steps = {{QColor("#00E5FF"), 40}, {QColor("#0A27D3"), 40}};
        profile.base.origin = "aqua-1";
        const BuildResult result = buildPackets(profile, Layout::gs76());
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join("; ")));
        const QByteArray element = powerPacket(result).mid(4 + 11 * 12, 12);
        QCOMPARE(element.toHex(' '), QByteArray("00 00 00 00 00 00 00 00 00 00 00 66"));
    }

    void protocolPowerReactive()
    {
        Profile profile;
        profile.base.color = QColor("#102030");
        profile.reactive = {true, QColor("#A0B0C0"), 0x0123};
        const BuildResult result = buildPackets(profile, Layout::gs76(), true);
        QCOMPARE(powerPacket(result).mid(4 + 11 * 12, 12).toHex(' '), QByteArray("10 20 30 a0 b0 c0 23 01 00 08 00 66"));
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

    static Document libDoc(const QString &name, const QString &color = "#112233")
    {
        Profile profile;
        profile.base.color = QColor(color);
        return fromLighting(profile, name);
    }

    void librarySaveLoad()
    {
        QTemporaryDir dir;
        Library lib(dir.path());
        QString error;
        const Document doc = libDoc("Tron");
        QVERIFY2(lib.save(doc, &error), qPrintable(error));
        QCOMPARE(lib.names(), QStringList{"Tron"});
        QVERIFY(QFile::exists(dir.path() + "/profiles/tron.klc.json"));
        QStringList errors;
        const auto loaded = lib.load("Tron", &errors);
        QVERIFY(loaded);
        QCOMPARE(QJsonDocument(toJson(*loaded)).toJson(), QJsonDocument(toJson(doc)).toJson());
        QVERIFY(!lib.load("Nope", &errors));
    }

    void libraryFileNameCollision()
    {
        QCOMPARE(Library::fileNameFor("Tron!", {"tron-"}), QString("tron--2"));
        QCOMPARE(Library::fileNameFor("", {}), QString("profile"));
        QTemporaryDir dir;
        Library lib(dir.path());
        QString error;
        QVERIFY(lib.save(libDoc("Tron!"), &error));
        QVERIFY(lib.save(libDoc("Tron?"), &error));
        QCOMPARE(lib.entries().size(), 2);
        QCOMPARE(QDir(dir.path() + "/profiles").entryList(QDir::Files).size(), 2);
    }

    void libraryRename()
    {
        QTemporaryDir dir;
        Library lib(dir.path());
        QString error;
        QVERIFY(lib.save(libDoc("A"), &error));
        lib.link("A", "tron");
        lib.setApplied("A");
        QVERIFY2(lib.rename("A", "B", &error), qPrintable(error));
        QVERIFY(QFile::exists(dir.path() + "/profiles/b.klc.json"));
        QVERIFY(!QFile::exists(dir.path() + "/profiles/a.klc.json"));
        QCOMPARE(lib.themeOf("B"), QString("tron"));
        QCOMPARE(lib.applied(), QString("B"));
        QStringList errors;
        QVERIFY(!lib.load("A", &errors));
        QCOMPARE(lib.load("B", &errors)->name, QString("B"));
        Library again(dir.path());
        QCOMPARE(again.applied(), QString("B"));
        QCOMPARE(again.profileForTheme("tron"), QString("B"));
    }

    void libraryRemove()
    {
        QTemporaryDir dir;
        Library lib(dir.path());
        QString error;
        QVERIFY(lib.save(libDoc("A"), &error));
        lib.link("A", "tron");
        lib.setApplied("A");
        QVERIFY2(lib.remove("A", &error), qPrintable(error));
        QVERIFY(lib.entries().isEmpty());
        QCOMPARE(lib.applied(), QString());
        QCOMPARE(lib.profileForTheme("tron"), QString());
    }

    // The links follow the stored name, whatever case the caller used.
    void libraryRemoveOtherCase()
    {
        QTemporaryDir dir;
        Library lib(dir.path());
        QString error;
        QVERIFY(lib.save(libDoc("Tron"), &error));
        lib.link("Tron", "tron");
        lib.setApplied("Tron");
        QVERIFY2(lib.remove("tron", &error), qPrintable(error));
        QVERIFY(lib.entries().isEmpty());
        QCOMPARE(lib.applied(), QString());
        QCOMPARE(lib.profileForTheme("tron"), QString());
        Library again(dir.path());
        QCOMPARE(again.applied(), QString());
        QCOMPARE(again.profileForTheme("tron"), QString());
    }

    void libraryMessagesTranslated()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "fr\n");
        Translator translator(dir.path() + "/language", ":/i18n");
        QCoreApplication::installTranslator(&translator);
        Library lib(dir.path());
        QStringList errors;
        QVERIFY(!lib.load("Nope", &errors));
        QString error;
        QVERIFY(!lib.rename("Nope", "B", &error));
        QString removeError;
        QVERIFY(!lib.remove("Nope", &removeError));
        QCoreApplication::removeTranslator(&translator);
        QCOMPARE(errors, QStringList{"Aucun profil nommé « Nope »"});
        QCOMPARE(error, QString("Aucun profil nommé « Nope »"));
        QCOMPARE(removeError, QString("Aucun profil nommé « Nope »"));
    }

    void libraryDefaultNameTranslated()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "fr\n");
        Translator translator(dir.path() + "/language", ":/i18n");
        QCoreApplication::installTranslator(&translator);
        const QString french = Library::defaultName();
        QCoreApplication::removeTranslator(&translator);
        QCOMPARE(french, QString("Défaut"));
        QCOMPARE(Library::defaultName(), QString("Default"));
    }

    // With no theme set up, the first start makes one profile, named in the user's language.
    void libraryMigrateDefaultTranslated()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "fr\n");
        Translator translator(dir.path() + "/language", ":/i18n");
        QCoreApplication::installTranslator(&translator);
        Library lib(dir.path() + "/klc");
        const bool ran = lib.migrate(fakeTheme(dir, "tokyo-night"));
        QCoreApplication::removeTranslator(&translator);
        QVERIFY(ran);
        QCOMPARE(lib.names(), QStringList{"Défaut"});
        QCOMPARE(lib.applied(), QString("Défaut"));
    }

    void libraryNameError()
    {
        QTemporaryDir dir;
        Library lib(dir.path());
        QString error;
        QVERIFY(lib.save(libDoc("A"), &error));
        QVERIFY(!lib.nameError("").isEmpty());
        QVERIFY(!lib.nameError("  ").isEmpty());
        QVERIFY(!lib.nameError("A").isEmpty());
        QCOMPARE(lib.nameError("A", "A"), QString());
        QCOMPARE(lib.nameError("B"), QString());
    }

    void libraryLinkOneToOne()
    {
        QTemporaryDir dir;
        Library lib(dir.path());
        QCOMPARE(lib.link("A", "tron"), QString());
        QCOMPARE(lib.link("B", "tron"), QString("A"));
        QCOMPARE(lib.themeOf("A"), QString());
        QCOMPARE(lib.link("B", "gruvbox"), QString());
        QCOMPARE(lib.themeOf("B"), QString("gruvbox"));
        QCOMPARE(lib.profileForTheme("tron"), QString());
        lib.link("B", "");
        QCOMPARE(lib.themeOf("B"), QString());
    }

    void libraryStateCorrupt()
    {
        QTemporaryDir dir;
        write(dir.path() + "/state.json", "{oops");
        Library lib(dir.path());
        QCOMPARE(lib.applied(), QString());
        QCOMPARE(lib.profileForTheme("tron"), QString());
        QVERIFY(QFile::exists(dir.path() + "/state.json.bad"));
        lib.setApplied("X");
        QFile file(dir.path() + "/state.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value("applied").toString(), QString("X"));
    }

    void libraryUnreadableEntry()
    {
        QTemporaryDir dir;
        QDir().mkpath(dir.path() + "/profiles");
        write(dir.path() + "/profiles/bad.klc.json", "{nope");
        Library lib(dir.path());
        QCOMPARE(lib.entries().size(), 1);
        QVERIFY(!lib.entries().first().readable);
        QStringList errors;
        QVERIFY(!lib.load(lib.entries().first().name, &errors));
        QVERIFY(!errors.isEmpty());
        QString error;
        QVERIFY(lib.save(libDoc("Good"), &error));
        QFile file(dir.path() + "/profiles/bad.klc.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("{nope"));
        QCOMPARE(lib.entries().size(), 2);
    }

    void libraryMigrate()
    {
        QTemporaryDir dir;
        ThemePaths paths = fakeTheme(dir, "tron");
        const QByteArray klc = "{\"version\":1,\"base\":{\"type\":\"static\",\"color\":\"#112233\"}}";
        QDir().mkpath(paths.userThemes + "/tron");
        QDir().mkpath(paths.stockThemes + "/tokyo-night");
        write(paths.userThemes + "/tron/keyboard.klc.json", klc);
        write(paths.userThemes + "/tron/keyboard.klc.json.essai", QByteArray(klc).replace("112233", "abcdef"));
        write(paths.stockThemes + "/tokyo-night/keyboard.rgb", "#445566\n");
        QDir().mkpath(paths.stockThemes + "/plain");
        Library lib(dir.path() + "/klc");
        QVERIFY(lib.migrate(paths));
        QCOMPARE(lib.names(), (QStringList{"Tokyo-night", "Tron"}));
        QCOMPARE(lib.profileForTheme("tron"), QString("Tron"));
        QCOMPARE(lib.profileForTheme("tokyo-night"), QString("Tokyo-night"));
        QCOMPARE(lib.profileForTheme("plain"), QString());
        QCOMPARE(lib.applied(), QString("Tron"));
        QStringList errors;
        QCOMPARE(lighting(*lib.load("Tokyo-night", &errors)).base.color, QColor("#445566"));
        QCOMPARE(lighting(*lib.load("Tron", &errors)).base.color, QColor("#112233"));
        QFile file(paths.userThemes + "/tron/keyboard.klc.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), klc);
        QVERIFY(!lib.migrate(paths));
        QCOMPARE(lib.names().size(), 2);
    }

    void libraryMigrateActiveThemeUnlit()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "plain");
        QDir().mkpath(paths.userThemes + "/tron");
        QDir().mkpath(paths.stockThemes + "/plain");
        write(paths.userThemes + "/tron/keyboard.rgb", "#112233\n");
        Library lib(dir.path() + "/klc");
        QVERIFY(lib.migrate(paths));
        QCOMPARE(lib.names(), QStringList{"Tron"});
        QCOMPARE(lib.applied(), QString());
    }

    void libraryStateCorruptKeepsEarlierBad()
    {
        QTemporaryDir dir;
        write(dir.path() + "/state.json", "{oops");
        write(dir.path() + "/state.json.bad", "{older");
        Library lib(dir.path());
        lib.setApplied("X");
        QFile older(dir.path() + "/state.json.bad");
        QVERIFY(older.open(QIODevice::ReadOnly));
        QCOMPARE(older.readAll(), QByteArray("{older"));
        QFile newer(dir.path() + "/state.json.bad.2");
        QVERIFY(newer.open(QIODevice::ReadOnly));
        QCOMPARE(newer.readAll(), QByteArray("{oops"));
    }

    void libraryCopyAside()
    {
        QTemporaryDir dir;
        Library::copyAside(dir.path() + "/state.json.bad", "{oops");
        QFile file(dir.path() + "/state.json.bad");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("{oops"));
    }

    void libraryNamesCaseAndTrim()
    {
        QTemporaryDir dir;
        Library lib(dir.path());
        QString error;
        QVERIFY(lib.save(libDoc("Tron"), &error));
        QVERIFY(!lib.nameError("tron").isEmpty());
        QVERIFY(lib.save(libDoc(" tron "), &error));
        QCOMPARE(lib.names(), QStringList{"tron"});
        QVERIFY(lib.save(libDoc("A"), &error));
        QVERIFY2(lib.rename("A", "  B  ", &error), qPrintable(error));
        QVERIFY(lib.names().contains("B"));
        QVERIFY(!lib.rename("B", "TRON", &error));
    }

    void libraryMigrateNothing()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "plain");
        QDir().mkpath(paths.stockThemes + "/plain");
        Library lib(dir.path() + "/klc");
        QVERIFY(lib.migrate(paths));
        QCOMPARE(lib.names(), QStringList{"Default"});
        QCOMPARE(lib.applied(), QString("Default"));
        QCOMPARE(lib.themeOf("Default"), QString());
        QStringList errors;
        const Profile profile = lighting(*lib.load("Default", &errors));
        QCOMPARE(profile.base.type, QString("static"));
        QCOMPARE(profile.base.color, QColor(Qt::black));
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


    // A library profile with a plain colour, optionally linked to a theme.
    static void addProfile(const QTemporaryDir &dir, const QString &name, const QString &color, const QString &theme = {})
    {
        Library lib(dir.path() + "/klc");
        Profile profile;
        profile.base.color = QColor(color);
        QString error;
        QVERIFY(lib.save(fromLighting(profile, name), &error));
        if (!theme.isEmpty())
            lib.link(name, theme);
    }

    void cliCheck()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tron");
        write(paths.stateTheme + "/keyboard.effect", "tron\n");
        QDir().mkpath(paths.userThemes + "/tron");
        write(paths.userThemes + "/tron/keyboard.effect", "tron\n");
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
        addProfile(dir, "Ares", "#111111");
        CliRun run(dir, fakeTheme(dir, "tokyo-night"));
        QCOMPARE(run.exec({"apply"}), 0);
        QVERIFY(run.log.events.isEmpty());
    }

    void cliSends()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tokyo-night");
        addProfile(dir, "Tokyo", "#7aa2f7", "tokyo-night");
        CliRun run(dir, paths);
        QCOMPARE(run.exec({"apply", "--theme"}), 0);
        QCOMPARE(run.log.events.count("send"), 5);
    }

    void cliJson()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tron");
        QDir().mkpath(paths.userThemes + "/tron");
        write(paths.userThemes + "/tron/keyboard.effect", "tron\n");
        CliRun run(dir, paths);
        QCOMPARE(run.exec({"apply", "--check", "--json"}), 0);
        const QJsonObject result = QJsonDocument::fromJson(run.out.toUtf8()).object();
        QCOMPARE(result.value("ok").toBool(), true);
        QCOMPARE(result.value("packets").toInt(), 12);
        QCOMPARE(result.value("source").toString(), QString("Tron"));
    }

    void cliThemeLinked()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tron");
        addProfile(dir, "Ares", "#111111");
        addProfile(dir, "Tron", "#222222", "tron");
        Library(dir.path() + "/klc").setApplied("Ares");
        CliRun run(dir, paths);
        QCOMPARE(run.exec({"apply", "--theme"}), 0);
        QVERIFY(run.log.events.contains("open"));
        QCOMPARE(run.log.packets.first().mid(4, 3), QByteArray::fromHex("222222"));
        QCOMPARE(Library(dir.path() + "/klc").applied(), QString("Tron"));
    }

    void cliThemeUnlinked()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "gruvbox");
        addProfile(dir, "Ares", "#111111", "tron");
        Library(dir.path() + "/klc").setApplied("Ares");
        CliRun run(dir, paths);
        QCOMPARE(run.exec({"apply", "--theme"}), 0);
        QVERIFY(!run.log.events.contains("open"));
        QCOMPARE(Library(dir.path() + "/klc").applied(), QString("Ares"));
    }

    void cliBootApplied()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tron");
        addProfile(dir, "Ares", "#111111");
        addProfile(dir, "Tron", "#222222", "tron");
        Library(dir.path() + "/klc").setApplied("Ares");
        CliRun run(dir, paths);
        QCOMPARE(run.exec({"apply"}), 0);
        QCOMPARE(run.log.packets.first().mid(4, 3), QByteArray::fromHex("111111"));
    }

    void cliBootNothingApplied()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tron");
        addProfile(dir, "Ares", "#111111");
        addProfile(dir, "Tron", "#222222", "tron");
        Library(dir.path() + "/klc").setApplied("");
        CliRun run(dir, paths);
        QCOMPARE(run.exec({"apply"}), 0);
        QCOMPARE(run.log.packets.first().mid(4, 3), QByteArray::fromHex("222222"));
    }

    // A boot apply whose applied profile is gone sends the theme's profile instead of failing.
    void cliBootStaleApplied()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tron");
        addProfile(dir, "Tron", "#222222", "tron");
        Library(dir.path() + "/klc").setApplied("Gone");
        CliRun check(dir, paths);
        QCOMPARE(check.exec({"apply", "--check"}), 0);
        QVERIFY(check.log.events.isEmpty());
        CliRun run(dir, paths);
        QCOMPARE(run.exec({"apply"}), 0);
        QCOMPARE(run.log.packets.first().mid(4, 3), QByteArray::fromHex("222222"));
        QCOMPARE(Library(dir.path() + "/klc").applied(), QString("Tron"));
    }

    void cliByName()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tron");
        addProfile(dir, "Ares", "#111111");
        addProfile(dir, "Tron", "#222222", "tron");
        CliRun run(dir, paths);
        QCOMPARE(run.exec({"apply", "ares"}), 0);
        QCOMPARE(run.log.packets.first().mid(4, 3), QByteArray::fromHex("111111"));
        QCOMPARE(Library(dir.path() + "/klc").applied(), QString("Ares"));
        CliRun other(dir, paths);
        QCOMPARE(other.exec({"apply", "Inconnu"}), 1);
        QVERIFY2(other.err.contains("Inconnu"), qPrintable(other.err));
        QVERIFY(!other.log.events.contains("open"));
    }

    void cliByPath()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tron");
        addProfile(dir, "Ares", "#111111");
        Library(dir.path() + "/klc").setApplied("Ares");
        const QString file = dir.path() + "/x.json";
        write(file, "{\"version\":1,\"base\":{\"type\":\"static\",\"color\":\"#334455\"}}");
        CliRun run(dir, paths);
        QCOMPARE(run.exec({"apply", file}), 0);
        QCOMPARE(run.log.packets.first().mid(4, 3), QByteArray::fromHex("334455"));
        QCOMPARE(Library(dir.path() + "/klc").applied(), QString("Ares"));
        // A version 2 document works too.
        Profile profile;
        profile.base.color = QColor("#556677");
        QString error;
        const QString file2 = dir.path() + "/y.json";
        QVERIFY(saveDocument(fromLighting(profile, "Y"), file2, &error));
        CliRun again(dir, paths);
        QCOMPARE(again.exec({"apply", file2}), 0);
        QCOMPARE(again.log.packets.first().mid(4, 3), QByteArray::fromHex("556677"));
    }

    void cliMigratesFirst()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "tron");
        QDir().mkpath(paths.userThemes + "/tron");
        write(paths.userThemes + "/tron/keyboard.klc.json", "{\"version\":1,\"base\":{\"type\":\"static\",\"color\":\"#112233\"}}");
        CliRun run(dir, paths);
        QCOMPARE(run.exec({"apply", "--theme"}), 0);
        QCOMPARE(run.log.packets.first().mid(4, 3), QByteArray::fromHex("112233"));
        QCOMPARE(Library(dir.path() + "/klc").names(), QStringList{"Tron"});
    }

    void cliThemeRapidSwitchLinked()
    {
        QTemporaryDir dir;
        const ThemePaths paths = fakeTheme(dir, "a");
        addProfile(dir, "A", "#111111", "a");
        addProfile(dir, "B", "#222222", "b");
        addProfile(dir, "C", "#333333", "c");
        CliRun run(dir, paths);
        QCOMPARE(run.exec({"apply", "--theme"}), 0);
        write(paths.themeName, "b\n");
        run.log.now += 1000;
        run.onSleep = [&](int ms) {
            if (ms > Device::PacketGapMs)
                write(paths.themeName, "c\n");
        };
        run.log.packets.clear();
        QCOMPARE(run.exec({"apply", "--theme"}), 0);
        QVERIFY(!run.log.packets.isEmpty());
        QCOMPARE(run.log.packets.first().mid(4, 3), QByteArray::fromHex("333333"));
        QCOMPARE(Library(dir.path() + "/klc").applied(), QString("C"));
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


    // Makes a group from the given keys and leaves it open.
    static void makeGroup(Backend *backend, const QString &name, const QStringList &keys)
    {
        backend->select(keys, false);
        QCOMPARE(backend->createGroup(name), QString());
    }

    void backendClickGroupedKeyOpensGroup()
    {
        EditorRun run;
        makeGroup(run.backend, "ZQSD", {"KEY_W", "KEY_A", "KEY_S", "KEY_D"});
        run.backend->openBase();
        run.backend->select({"KEY_A"}, false);
        QCOMPARE(run.backend->currentKind(), QString("group"));
        QCOMPARE(run.backend->currentGroup(), 0);
        QCOMPARE(run.backend->groupKeys().size(), 4);
        QCOMPARE(run.backend->selection(), QStringList{"KEY_A"});
    }

    void backendClickFreeKeyOffersGroup()
    {
        EditorRun run;
        run.backend->select({"KEY_ESC"}, false);
        QCOMPARE(run.backend->currentKind(), QString("selection"));
        QCOMPARE(run.backend->currentGroup(), -1);
        const QString label = KeyLabels::fromRmlvo("fr", "").label(*Layout::gs76().byId("KEY_ESC"));
        QVERIFY(!label.isEmpty());
        QCOMPARE(run.backend->defaultGroupName(), label);
        run.backend->select({"KEY_ESC", "KEY_Q"}, false);
        QCOMPARE(run.backend->defaultGroupName(), QString("Group 1"));
    }

    void backendCtrlClickOutsideGroup()
    {
        EditorRun run;
        makeGroup(run.backend, "ZQSD", {"KEY_W", "KEY_A", "KEY_S", "KEY_D"});
        run.backend->select({"KEY_A"}, false);
        run.backend->select({"KEY_S"}, true);
        QCOMPARE(run.backend->currentKind(), QString("group"));
        QCOMPARE(run.backend->selection(), QStringList({"KEY_A", "KEY_S"}));
        run.backend->select({"KEY_ESC"}, true);
        QCOMPARE(run.backend->currentKind(), QString("selection"));
        QCOMPARE(run.backend->currentGroup(), -1);
        // The quick buttons are a free selection too, unless every key is in the open group.
        run.backend->openGroup(0);
        run.backend->selectGroup("wasd");
        QCOMPARE(run.backend->currentKind(), QString("group"));
        run.backend->openBase();
        run.backend->selectGroup("wasd");
        QCOMPARE(run.backend->currentKind(), QString("selection"));
    }

    void backendCreateGroupMovesKeys()
    {
        EditorRun run;
        makeGroup(run.backend, "ZQSD", {"KEY_W", "KEY_A", "KEY_S", "KEY_D"});
        run.backend->select({"KEY_W", "KEY_ESC"}, false);
        QCOMPARE(run.backend->currentKind(), QString("selection"));
        QCOMPARE(run.backend->createGroup("Mix"), QString());
        const Document &doc = run.backend->document();
        QCOMPARE(doc.groups.size(), 2);
        QCOMPARE(doc.groups[0].keys, QStringList({"KEY_A", "KEY_S", "KEY_D"}));
        QCOMPARE(doc.groups[1].name, QString("Mix"));
        QCOMPARE(doc.groups[1].keys, QStringList({"KEY_W", "KEY_ESC"}));
        QCOMPARE(run.backend->currentKind(), QString("group"));
        QCOMPARE(run.backend->currentGroup(), 1);
        QCOMPARE(run.backend->groups().size(), 2);
        QCOMPARE(run.backend->groups()[1].toMap()["count"].toInt(), 2);
        // Emptied, a group is removed.
        run.backend->select({"KEY_A", "KEY_S", "KEY_D"}, false);
        run.backend->addSelectionToGroup(1);
        QCOMPARE(run.backend->document().groups.size(), 1);
        QCOMPARE(run.backend->document().groups[0].keys.size(), 5);
        QCOMPARE(run.backend->currentKind(), QString("group"));
        QCOMPARE(run.backend->currentGroup(), 0);
        // Taking keys out of the open group frees them; all of them removes the group.
        run.backend->select({"KEY_W"}, false);
        run.backend->removeSelectionFromGroup();
        QCOMPARE(run.backend->document().groups[0].keys.size(), 4);
        QCOMPARE(run.backend->document().groupOf("KEY_W"), -1);
        run.backend->select({"KEY_A", "KEY_S", "KEY_D", "KEY_ESC"}, true);
        QCOMPARE(run.backend->currentKind(), QString("group"));
        run.backend->removeSelectionFromGroup();
        QVERIFY(run.backend->document().groups.isEmpty());
        QCOMPARE(run.backend->currentKind(), QString("base"));
    }

    void backendGroupNameRules()
    {
        EditorRun run;
        makeGroup(run.backend, "ZQSD", {"KEY_W", "KEY_A", "KEY_S", "KEY_D"});
        makeGroup(run.backend, "Esc", {"KEY_ESC"});
        const QJsonObject before = toJson(run.backend->document());
        run.backend->select({"KEY_Q"}, false);
        for (const QString &name : {QString(), QString("  "), QString("ZQSD"), QString(" ZQSD ")})
            QVERIFY2(!run.backend->createGroup(name).isEmpty(), qPrintable(name));
        QVERIFY(!run.backend->renameGroup(1, "").isEmpty());
        QVERIFY(!run.backend->renameGroup(1, "ZQSD").isEmpty());
        QCOMPARE(toJson(run.backend->document()), before);
        QVERIFY(run.backend->canUndo());
        run.backend->undo();
        run.backend->undo();
        QVERIFY(!run.backend->canUndo());
        run.backend->redo();
        run.backend->redo();
        QCOMPARE(run.backend->renameGroup(1, " Échap "), QString());
        QCOMPARE(run.backend->document().groups[1].name, QString("Échap"));
        QCOMPARE(run.backend->renameGroup(1, "Échap"), QString());
    }

    void backendBehaviorSwitch()
    {
        EditorRun run;
        makeGroup(run.backend, "ZQSD", {"KEY_W", "KEY_A", "KEY_S", "KEY_D"});
        run.backend->setColor(QColor("#FF0000"));
        QCOMPARE(run.backend->setBehavior("pulse"), QString());
        const Group &pulse = run.backend->document().groups[0];
        QCOMPARE(pulse.behavior, QString("pulse"));
        QCOMPARE(pulse.steps.size(), 1);
        QCOMPARE(pulse.steps[0].color, QColor("#FF0000"));
        QVERIFY(validate(run.backend->document(), Layout::gs76()).isEmpty());
        QCOMPARE(run.backend->current()["behavior"].toString(), QString("pulse"));
        run.backend->setStepColor(0, QColor("#00FF00"));
        QCOMPARE(run.backend->setBehavior("wave"), QString());
        QVERIFY(validate(run.backend->document(), Layout::gs76()).isEmpty());
        QCOMPARE(run.backend->setBehavior("static"), QString());
        QCOMPARE(run.backend->document().groups[0].color, QColor("#00FF00"));
        QCOMPARE(run.backend->current()["color"].value<QColor>(), QColor("#00FF00"));
    }

    void backendAnimatedLimit()
    {
        EditorRun run;
        for (int i = 1; i <= 7; ++i)
            makeGroup(run.backend, QString("F%1").arg(i), {QString("KEY_F%1").arg(i)});
        for (int i = 0; i < 6; ++i) {
            run.backend->openGroup(i);
            QCOMPARE(run.backend->setBehavior("pulse"), QString());
        }
        QCOMPARE(run.backend->animatedCount(), 6);
        run.backend->openGroup(6);
        const QJsonObject before = toJson(run.backend->document());
        QCOMPARE(run.backend->setBehavior("pulse"), QString("At most 6 animated groups (controller limit)"));
        QCOMPARE(toJson(run.backend->document()), before);
        QCOMPARE(run.backend->animatedCount(), 6);
    }

    void backendPaintGroup()
    {
        EditorRun run;
        QVERIFY(!run.backend->dirty());
        makeGroup(run.backend, "ZQSD", {"KEY_W", "KEY_A", "KEY_S", "KEY_D"});
        run.backend->setColor(QColor("#FF0000"));
        QCOMPARE(run.backend->document().groups[0].color, QColor("#FF0000"));
        QCOMPARE(run.backend->previewColor("KEY_W", 0), QColor("#FF0000"));
        QVERIFY(run.backend->dirty());
        // A free selection has no color of its own: nothing is painted.
        run.backend->select({"KEY_ESC"}, false);
        const QJsonObject before = toJson(run.backend->document());
        run.backend->setColor(QColor("#00FF00"));
        QCOMPARE(toJson(run.backend->document()), before);
    }

    void backendGroupAnimation()
    {
        EditorRun run;
        run.backend->selectGroup("function");
        QCOMPARE(run.backend->createGroup("F1–F12"), QString());
        QCOMPARE(run.backend->setBehavior("wave"), QString());
        QCOMPARE(lighting(run.backend->document()).effects.size(), 1);
        QCOMPARE(lighting(run.backend->document()).effects.first().keys.size(), 12);
        run.backend->setStepColor(0, QColor("#ABCDEF"));
        QCOMPARE(run.backend->document().groups[0].steps.first().color, QColor("#ABCDEF"));
        QCOMPARE(run.backend->document().groups[0].start, QColor("#ABCDEF"));
        run.backend->addStep();
        QCOMPARE(run.backend->document().groups[0].steps.size(), 2);
        run.backend->setStepDuration(1, 77);
        QCOMPARE(run.backend->document().groups[0].steps[1].duration, 77);
        run.backend->removeStep(0);
        QCOMPARE(run.backend->document().groups[0].start, run.backend->document().groups[0].steps.first().color);
        run.backend->setOrigin("aqua-3");
        QCOMPARE(run.backend->current()["origin"].toString(), QString("aqua-3"));
        run.backend->deleteGroup(0);
        QVERIFY(run.backend->document().groups.isEmpty());
        QCOMPARE(run.backend->currentKind(), QString("base"));
    }

    void backendApplySaves()
    {
        EditorRun run;
        run.backend->openBase();
        run.backend->setColor(QColor("#123456"));
        run.backend->apply();
        QTRY_COMPARE(run.sent, 1);
        QTRY_VERIFY(!run.backend->busy());
        QVERIFY(!run.backend->dirty());
        QVERIFY(run.backend->error().isEmpty());
        QStringList errors;
        const auto saved = Library(run.dir.path() + "/klc").load("Default", &errors);
        QVERIFY2(saved, qPrintable(errors.join("; ")));
        QCOMPARE(saved->base.color, QColor("#123456"));
        QVERIFY(run.backend->cooldownMs() > 0);
    }

    // `applied` names what the keyboard shows: only a send that worked changes it.
    void backendApplyFailureKeepsApplied()
    {
        EditorRun run;
        const QString klc = run.dir.path() + "/klc";
        Library(klc).setApplied("Other");
        run.result = {ApplyError::KeyboardAbsent, "absent"};
        run.backend->setColor(QColor("#123456"));
        run.backend->apply();
        QTRY_COMPARE(run.sent, 1);
        QTRY_VERIFY(!run.backend->busy());
        QCOMPARE(Library(klc).applied(), QString("Other"));
        QCOMPARE(run.backend->error(), QString("absent"));
    }

    void backendApplySuccessSetsApplied()
    {
        EditorRun run;
        const QString klc = run.dir.path() + "/klc";
        Library(klc).setApplied("Other");
        run.backend->setColor(QColor("#123456"));
        run.backend->apply();
        QTRY_COMPARE(run.sent, 1);
        QTRY_COMPARE(Library(klc).applied(), QString("Default"));
    }

    void backendOpensApplied()
    {
        EditorRun run;
        seed(run, {"A", "B"}, "B");
        QCOMPARE(run.backend->profileName(), QString("B"));
        QCOMPARE(run.backend->appliedName(), QString("B"));
        QCOMPARE(run.backend->profiles(), QStringList({"A", "B"}));
    }

    void backendNewDuplicateRename()
    {
        EditorRun run;
        const QString klc = run.dir.path() + "/klc";
        QCOMPARE(run.backend->newProfile("C"), QString());
        QCOMPARE(run.backend->profileName(), QString("C"));
        QVERIFY(run.backend->document().groups.isEmpty());
        QCOMPARE(run.backend->document().base.type, QString("static"));
        QCOMPARE(run.backend->document().base.color, QColor(Qt::black));
        QVERIFY(Library(klc).names().contains("C"));

        makeGroup(run.backend, "F1", {"KEY_F1"});
        QVERIFY(run.backend->save());
        QCOMPARE(run.backend->duplicateProfile("C (copy)"), QString());
        QCOMPARE(run.backend->profileName(), QString("C (copy)"));
        QCOMPARE(run.backend->document().groups.size(), 1);
        QVERIFY(!run.backend->dirty());
        QVERIFY(Library(klc).names().contains("C (copy)"));

        // Empty or taken (whatever the case): refused, nothing changes.
        const QStringList before = run.backend->profiles();
        QVERIFY(!run.backend->newProfile("").isEmpty());
        QVERIFY(!run.backend->newProfile("c").isEmpty());
        QVERIFY(!run.backend->duplicateProfile(" ").isEmpty());
        QVERIFY(!run.backend->renameProfile("C").isEmpty());
        QVERIFY(!run.backend->renameProfile("").isEmpty());
        QCOMPARE(run.backend->profiles(), before);
        QCOMPARE(run.backend->profileName(), QString("C (copy)"));

        QCOMPARE(run.backend->renameProfile("D"), QString());
        QVERIFY(run.backend->profiles().contains("D"));
        QVERIFY(!run.backend->profiles().contains("C (copy)"));
        QCOMPARE(run.backend->profileName(), QString("D"));
        QCOMPARE(run.backend->nameError("d").isEmpty(), false);
        QCOMPARE(run.backend->nameError("Z"), QString());
    }

    void backendSwitchResetsHistory()
    {
        EditorRun run;
        seed(run, {"A", "B"}, "A");
        run.backend->setColor(QColor("#123456"));
        QVERIFY(run.backend->canUndo());
        QVERIFY(run.backend->save());
        QVERIFY(run.backend->openProfile("B"));
        QCOMPARE(run.backend->profileName(), QString("B"));
        QVERIFY(!run.backend->canUndo());
        QVERIFY(!run.backend->canRedo());
        QVERIFY(!run.backend->dirty());
        QVERIFY(run.backend->openProfile("A"));
        QCOMPARE(run.backend->document().base.color, QColor("#123456"));
    }

    void backendSaveDoesNotSend()
    {
        EditorRun run;
        seed(run, {"A"}, "A");
        run.backend->setColor(QColor("#123456"));
        QVERIFY(run.backend->dirty());
        QVERIFY(run.backend->save());
        QVERIFY(!run.backend->dirty());
        QTest::qWait(50);
        QCOMPARE(run.sent, 0);
        QStringList errors;
        const auto saved = Library(run.dir.path() + "/klc").load("A", &errors);
        QVERIFY(saved);
        QCOMPARE(saved->base.color, QColor("#123456"));
        QCOMPARE(Library(run.dir.path() + "/klc").applied(), QString("A"));
    }

    void backendLinkTheme()
    {
        EditorRun run;
        const QString klc = run.dir.path() + "/klc";
        seed(run, {"A", "B"}, "B");
        Library(klc).link("A", "tron");
        QVERIFY(run.backend->openProfile("B"));
        QCOMPARE(run.backend->linkTheme("tron"), QString("A"));
        QCOMPARE(run.backend->linkedTheme(), QString("tron"));
        QCOMPARE(Library(klc).profileForTheme("tron"), QString("B"));
        QVERIFY(!run.backend->canUndo());
        QVERIFY(!run.backend->dirty());
        // Linking again to the same profile reports nothing; "" unlinks.
        QCOMPARE(run.backend->linkTheme("tron"), QString());
        QCOMPARE(run.backend->linkTheme(""), QString());
        QCOMPARE(run.backend->linkedTheme(), QString());
    }

    // A theme hook can change state.json while the editor is open: the editor must not undo it.
    void backendKeepsExternalStateChanges()
    {
        EditorRun run;
        const QString klc = run.dir.path() + "/klc";
        seed(run, {"A", "B"}, "A");
        Library(klc).link("B", "tron");
        Library(klc).setApplied("B");
        QCOMPARE(run.backend->appliedName(), QString("B"));
        QCOMPARE(run.backend->linkTheme("nord"), QString());
        QCOMPARE(Library(klc).profileForTheme("tron"), QString("B"));
        QCOMPARE(Library(klc).applied(), QString("B"));
        Library(klc).link("B", "tron2");
        QCOMPARE(run.backend->renameProfile("A2"), QString());
        QCOMPARE(Library(klc).profileForTheme("tron2"), QString("B"));
        QCOMPARE(Library(klc).applied(), QString("B"));
    }

    void backendNoProfileOpsWhileSending()
    {
        EditorRun run;
        const QString klc = run.dir.path() + "/klc";
        seed(run, {"A", "B"}, "B");
        QVERIFY(run.backend->openProfile("A"));
        run.hold = true;
        run.backend->apply();
        QTRY_COMPARE(run.sent, 1);
        QVERIFY(run.backend->busy());
        run.backend->deleteProfile();
        QVERIFY(!run.backend->renameProfile("Z").isEmpty());
        QVERIFY(!run.backend->newProfile("N").isEmpty());
        QVERIFY(!run.backend->duplicateProfile("M").isEmpty());
        QVERIFY(!run.backend->openProfile("B"));
        QCOMPARE(Library(klc).names(), QStringList({"A", "B"}));
        QCOMPARE(run.backend->profileName(), QString("A"));
        QCOMPARE(Library(klc).applied(), QString("B"));
        run.hold = false;
        QTRY_COMPARE(Library(klc).applied(), QString("A"));
        QTRY_VERIFY(!run.backend->busy());
    }

    void backendRenameAppliedLinked()
    {
        EditorRun run;
        const QString klc = run.dir.path() + "/klc";
        seed(run, {"A"}, "A");
        Library(klc).link("A", "tron");
        QCOMPARE(run.backend->renameProfile("B"), QString());
        QCOMPARE(run.backend->appliedName(), QString("B"));
        QCOMPARE(run.backend->linkedTheme(), QString("tron"));
        QCOMPARE(Library(klc).profileForTheme("tron"), QString("B"));
    }

    void backendDeleteApplied()
    {
        EditorRun run;
        const QString klc = run.dir.path() + "/klc";
        seed(run, {"A", "B", "C"}, "B");
        QCOMPARE(run.backend->profileName(), QString("B"));
        run.backend->deleteProfile();
        QCOMPARE(run.backend->appliedName(), QString());
        QCOMPARE(run.backend->profileName(), QString("C"));
        QCOMPARE(run.backend->profiles(), QStringList({"A", "C"}));
        QVERIFY(!run.backend->dirty());
    }

    void backendDeleteLastProfile()
    {
        EditorRun run;
        seed(run, {"A"}, "A");
        run.backend->deleteProfile();
        QCOMPARE(run.backend->profileName(), QString("Default"));
        QCOMPARE(run.backend->profiles(), QStringList({"Default"}));
        QVERIFY(run.backend->document().groups.isEmpty());
        QCOMPARE(run.backend->document().base.color, QColor(Qt::black));
    }

    void backendUnreadableProfile()
    {
        EditorRun run;
        const QString klc = run.dir.path() + "/klc";
        seed(run, {"A", "B"}, "A");
        QFile bad(klc + "/profiles/broken.klc.json");
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{ not json");
        bad.close();
        QStringList unreadable;
        for (const LibraryEntry &entry : Library(klc).entries())
            if (!entry.readable)
                unreadable.append(entry.name);
        QCOMPARE(unreadable.size(), 1);
        run.backend->setColor(QColor("#123456"));
        QVERIFY(!run.backend->openProfile(unreadable.first()));
        QVERIFY(!run.backend->error().isEmpty());
        QCOMPARE(run.backend->profileName(), QString("A"));
        QVERIFY(run.backend->dirty());
        QCOMPARE(run.backend->document().base.color, QColor("#123456"));
        QVERIFY(!run.backend->openProfile("nope"));
    }

    void backendListsUnreadableProfiles()
    {
        EditorRun run;
        const QString klc = run.dir.path() + "/klc";
        seed(run, {"A"}, "A");
        QCOMPARE(run.backend->unreadableProfiles(), QStringList());
        write(klc + "/profiles/broken.klc.json", "{ not json");
        QCOMPARE(run.backend->unreadableProfiles(), QStringList{"broken"});
        QVERIFY(run.backend->profiles().contains("broken"));
    }

    void backendNameErrorExcept()
    {
        EditorRun run;
        seed(run, {"tron", "B"}, "tron");
        QVERIFY(!run.backend->nameError("Tron").isEmpty());
        QCOMPARE(run.backend->nameError("Tron", "tron"), QString());
        QVERIFY(!run.backend->nameError("b", "tron").isEmpty());
    }

    // The last profile deleted, the replacement is named in the user's language.
    void backendDefaultNameTranslated()
    {
        EditorRun run;
        seed(run, {"A"}, "A");
        write(run.dir.path() + "/language", "fr\n");
        Translator translator(run.dir.path() + "/language", ":/i18n");
        QCoreApplication::installTranslator(&translator);
        run.backend->deleteProfile();
        QCoreApplication::removeTranslator(&translator);
        QCOMPARE(run.backend->profileName(), QString("Défaut"));
        QCOMPARE(run.backend->profiles(), QStringList({"Défaut"}));
    }

    void backendSaveFailure()
    {
        EditorRun run;
        seed(run, {"A"}, "A");
        const QString profiles = run.dir.path() + "/klc/profiles";
        run.backend->setColor(QColor("#123456"));
        QFile::setPermissions(profiles, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        const bool saved = run.backend->save();
        QFile::setPermissions(profiles, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QVERIFY(!saved);
        QVERIFY(!run.backend->error().isEmpty());
        QVERIFY(run.backend->dirty());
    }

    void backendApplyInvalid()
    {
        EditorRun run;
        makeGroup(run.backend, "F1", {"KEY_F1"});
        QCOMPARE(run.backend->setBehavior("pulse"), QString());
        // The step and the closing transition back to the start add up past 65535.
        run.backend->setStepDuration(0, 40000);
        const QString file = Library(run.dir.path() + "/klc").entries().first().path;
        QFile before(file);
        QVERIFY(before.open(QIODevice::ReadOnly));
        const QByteArray bytes = before.readAll();
        run.backend->apply();
        QVERIFY2(run.backend->error().contains("65535"), qPrintable(run.backend->error()));
        QTest::qWait(50);
        QCOMPARE(run.sent, 0);
        QFile after(file);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), bytes);
    }

    void backendUndoGroups()
    {
        EditorRun run;
        const QJsonObject initial = toJson(run.backend->document());
        makeGroup(run.backend, "ZQSD", {"KEY_W", "KEY_A", "KEY_S", "KEY_D"});
        QCOMPARE(run.backend->renameGroup(0, "WASD"), QString());
        run.backend->deleteGroup(0);
        QVERIFY(run.backend->document().groups.isEmpty());
        run.backend->undo();
        QCOMPARE(run.backend->document().groups[0].name, QString("WASD"));
        run.backend->undo();
        QCOMPARE(run.backend->document().groups[0].name, QString("ZQSD"));
        run.backend->undo();
        QCOMPARE(toJson(run.backend->document()), initial);
        QVERIFY(!run.backend->canUndo());
        QCOMPARE(run.backend->currentKind(), QString("base"));
        run.backend->redo();
        QCOMPARE(run.backend->document().groups.size(), 1);
    }

    void backendBaseEffectShowsOnWholeKeyboard()
    {
        // The ares preset's effect groups cover almost every key, so a base effect has to
        // replace them to be seen at all, as in Mystic Light; plain-color groups stay.
        EditorRun run("ares");
        QCOMPARE(run.backend->animatedCount(), 6);
        makeGroup(run.backend, "Esc", {"KEY_ESC"});
        const int groups = run.backend->document().groups.size();
        run.backend->openBase();
        run.backend->setBaseType("rainbow");
        QVERIFY(lighting(run.backend->document()).effects.isEmpty());
        // The animated base takes one of the controller's six slots.
        QCOMPARE(run.backend->animatedCount(), 1);
        QCOMPARE(run.backend->document().groups.size(), 1);
        QCOMPARE(run.backend->document().groups[0].name, QString("Esc"));
        QVERIFY(run.backend->notice().contains("6"));
        QVERIFY(run.backend->previewColor("KEY_Q", 0) != run.backend->previewColor("KEY_Q", 900));
        run.backend->undo();
        QCOMPARE(run.backend->animatedCount(), 6);
        QCOMPARE(run.backend->document().groups.size(), groups);
        QCOMPARE(run.backend->document().base.type, QString("static"));
    }

    void backendKeysDoNotOverlap()
    {
        EditorRun run;
        const QVariantList keys = run.backend->keys();
        QCOMPARE(keys.size(), 102);
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


    // Every user-visible string in the sources has a French entry, and the
    // catalog holds nothing the sources no longer use. Strings that read the
    // same in French (OK, R, G, B) carry an identical entry on purpose.
    void frenchCatalogCoversNewStrings()
    {
        const QString srcDir = QString(KLC_FIXTURES) + "/../../src";
        QFile catalog(srcDir + "/i18n/fr.json");
        QVERIFY(catalog.open(QIODevice::ReadOnly));
        const QJsonObject strings = QJsonDocument::fromJson(catalog.readAll()).object();
        QVERIFY(!strings.isEmpty());

        const QString quoted = "\"(?:[^\"\\\\]|\\\\.)*\"";
        const QRegularExpression call("(?:\\bqsTr|\\btr|\\btranslate\\(\\s*" + quoted + "\\s*,)\\s*\\(?\\s*((?:" + quoted + "\\s*)+)");
        const QRegularExpression literal("\"((?:[^\"\\\\]|\\\\.)*)\"");
        QSet<QString> used;
        const QFileInfoList files = QDir(srcDir).entryInfoList({"*.qml", "*.cpp"}, QDir::Files);
        QVERIFY(!files.isEmpty());
        for (const QFileInfo &info : files) {
            QFile file(info.filePath());
            QVERIFY(file.open(QIODevice::ReadOnly));
            auto matches = call.globalMatch(QString::fromUtf8(file.readAll()));
            while (matches.hasNext()) {
                QString text;
                auto parts = literal.globalMatch(matches.next().captured(1));
                while (parts.hasNext())
                    text += parts.next().captured(1);
                text.replace("\\\"", "\"").replace("\\n", "\n").replace("\\\\", "\\");
                used.insert(text);
                QVERIFY2(strings.contains(text), qPrintable(info.fileName() + ": no French entry for \"" + text + "\""));
            }
        }
        QVERIFY(used.size() > 50);
        for (const QString &key : strings.keys())
            QVERIFY2(used.contains(key), qPrintable("fr.json entry not used by any source: \"" + key + "\""));
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
        const EffectDef def = baseEffect(profile.base);
        const QList<Transition> list = transitions(def, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        for (const Transition &t : list)
            QVERIFY(qAbs(t.dr) <= 127 && qAbs(t.dg) <= 127 && qAbs(t.db) <= 127);
        const QList<QColor> reached = simulate(def.start, list);
        QVERIFY2(reached.size() == list.size(), "a channel wraps");
        for (int i = 0; i < def.steps.size(); ++i)
            QVERIFY2(near(reached[i], def.steps[i].color, 2), qPrintable(reached[i].name()));
        QVERIFY2(near(reached.last(), def.start, 2), qPrintable(reached.last().name()));
    }

    void effectsTotalDurationFits()
    {
        const EffectDef def{Qt::red, {{Qt::black, 40000}}, 40000, "aqua-1"};
        QString error;
        transitions(def, &error);
        QVERIFY2(error.contains("65535"), qPrintable(error));
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

    // ---- Profile bar and Save ----

    static QVariant callText(QQuickItem *box, int i)
    {
        QString text;
        QMetaObject::invokeMethod(box, "textAt", Q_RETURN_ARG(QString, text), Q_ARG(int, i));
        return text;
    }

    void uiProfileBoxListsProfiles()
    {
        Ui ui([](EditorRun &run) { seed(run, {"A", "B"}, "B"); });
        QQuickItem *box = nullptr;
        QTRY_VERIFY((box = findItem(ui.window->contentItem(), "profileBox")) != nullptr);
        QTRY_COMPARE(box->property("count").toInt(), 2);
        QVERIFY(!callText(box, 0).toString().contains("applied"));
        QVERIFY2(callText(box, 1).toString().contains("applied"), qPrintable(callText(box, 1).toString()));
        QCOMPARE(box->property("currentIndex").toInt(), 1); // the open profile: the applied one
    }

    // An unreadable profile is listed, marked as such (spec §7).
    void uiProfileBoxMarksUnreadable()
    {
        Ui ui([](EditorRun &run) {
            seed(run, {"A"}, "A");
            write(run.dir.path() + "/klc/profiles/broken.klc.json", "{ not json");
        });
        QQuickItem *box = findItem(ui.window->contentItem(), "profileBox");
        QVERIFY(box);
        QTRY_COMPARE(box->property("count").toInt(), 2);
        QVERIFY(!callText(box, 0).toString().contains("unreadable"));
        QCOMPARE(callText(box, 1).toString(), QString("broken · unreadable"));
    }

    void uiSwitchWithChangesAsks()
    {
        Ui ui([](EditorRun &run) { seed(run, {"A", "B"}, "A"); });
        QCOMPARE(ui.run.backend->profileName(), QString("A"));
        ui.run.backend->setColor(QColor("#123456"));
        QQuickItem *box = findItem(ui.window->contentItem(), "profileBox");
        QVERIFY(box);
        box->setProperty("currentIndex", 1);
        QMetaObject::invokeMethod(box, "activated", Q_ARG(int, 1));
        auto *dialog = ui.window->findChild<QObject *>("switchDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(ui.run.backend->profileName(), QString("A"));
        QMetaObject::invokeMethod(dialog, "discarded");
        QTRY_COMPARE(ui.run.backend->profileName(), QString("B"));
        QVERIFY(!ui.run.backend->dirty());
        QTRY_COMPARE(box->property("currentIndex").toInt(), 1);
    }

    // New with unsaved changes asks first, like switching: nothing is lost behind the user's back.
    void uiNewWithChangesAsks()
    {
        Ui ui([](EditorRun &run) { seed(run, {"A", "B"}, "A"); });
        ui.run.backend->setColor(QColor("#123456"));
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, itemCentre(findItem(ui.window->contentItem(), "newProfileButton")));
        auto *dialog = ui.window->findChild<QObject *>("switchDialog");
        auto *nameDialog = ui.window->findChild<QObject *>("profileNameDialog");
        QVERIFY(dialog && nameDialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(!nameDialog->property("visible").toBool());
        QCOMPARE(ui.run.backend->profileName(), QString("A"));
        QVERIFY(ui.run.backend->dirty());
        QMetaObject::invokeMethod(dialog, "discarded");
        QTRY_VERIFY(nameDialog->property("opened").toBool());
        QQuickItem *field = findItem(ui.window->contentItem(), "profileNameField");
        QVERIFY(field && field->isVisible());
        QCOMPARE(field->property("text").toString(), QString());
        field->setProperty("text", "C");
        QQuickItem *ok = findItem(ui.window->contentItem(), "profileNameOk");
        QTRY_VERIFY(ok->isEnabled());
        QMetaObject::invokeMethod(ok, "clicked");
        QTRY_COMPARE(ui.run.backend->profileName(), QString("C"));
        QVERIFY(!ui.run.backend->dirty());
        ui.run.backend->openProfile("A");
        QVERIFY(ui.run.backend->baseSummary().value("color").value<QColor>() != QColor("#123456"));
    }

    // Save in that dialog keeps the changes, then asks for the new profile's name.
    void uiNewWithChangesSaves()
    {
        Ui ui([](EditorRun &run) { seed(run, {"A"}, "A"); });
        ui.run.backend->setColor(QColor("#123456"));
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, itemCentre(findItem(ui.window->contentItem(), "newProfileButton")));
        auto *dialog = ui.window->findChild<QObject *>("switchDialog");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QMetaObject::invokeMethod(dialog, "accepted");
        auto *nameDialog = ui.window->findChild<QObject *>("profileNameDialog");
        QTRY_VERIFY(nameDialog->property("opened").toBool());
        QVERIFY(!ui.run.backend->dirty());
        QCOMPARE(ui.run.backend->profileName(), QString("A"));
        QCOMPARE(ui.run.backend->baseSummary().value("color").value<QColor>(), QColor("#123456"));
    }

    void uiSwitchWithoutChangesOpens()
    {
        Ui ui([](EditorRun &run) { seed(run, {"A", "B"}, "A"); });
        QQuickItem *box = findItem(ui.window->contentItem(), "profileBox");
        QVERIFY(box);
        QMetaObject::invokeMethod(box, "activated", Q_ARG(int, 1));
        QTRY_COMPARE(ui.run.backend->profileName(), QString("B"));
    }

    void uiSwitchSaveKeepsChanges()
    {
        Ui ui([](EditorRun &run) { seed(run, {"A", "B"}, "A"); });
        ui.run.backend->setColor(QColor("#123456"));
        QQuickItem *box = findItem(ui.window->contentItem(), "profileBox");
        QMetaObject::invokeMethod(box, "activated", Q_ARG(int, 1));
        auto *dialog = ui.window->findChild<QObject *>("switchDialog");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QMetaObject::invokeMethod(dialog, "accepted");
        QTRY_COMPARE(ui.run.backend->profileName(), QString("B"));
        ui.run.backend->openProfile("A");
        QCOMPARE(ui.run.backend->baseSummary().value("color").value<QColor>(), QColor("#123456"));
    }

    void uiSwitchDialogButtonsAreFrench()
    {
        Ui ui([](EditorRun &run) { seed(run, {"A", "B"}, "A"); });
        QCoreApplication::installTranslator(&ui.translator);
        QObject::connect(&ui.translator, &Translator::changed, &ui.engine, &QQmlEngine::retranslate);
        write(ui.langDir.path() + "/language", "fr\n");
        ui.run.backend->setColor(QColor("#123456"));
        QQuickItem *box = findItem(ui.window->contentItem(), "profileBox");
        QMetaObject::invokeMethod(box, "activated", Q_ARG(int, 1));
        auto *dialog = ui.window->findChild<QObject *>("switchDialog");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QQuickItem *save = findItem(ui.window->contentItem(), "switchSave");
        QQuickItem *discard = findItem(ui.window->contentItem(), "switchDiscard");
        QQuickItem *cancel = findItem(ui.window->contentItem(), "switchCancel");
        QVERIFY(save && discard && cancel);
        QTRY_COMPARE(save->property("text").toString(), QString("Enregistrer"));
        QCOMPARE(discard->property("text").toString(), QString("Abandonner"));
        QCOMPARE(cancel->property("text").toString(), QString("Annuler"));
        QCoreApplication::removeTranslator(&ui.translator);
    }

    void uiSaveShortcut()
    {
        Ui ui;
        ui.run.backend->setColor(QColor("#123456"));
        QVERIFY(ui.run.backend->dirty());
        QTest::keySequence(ui.window, QKeySequence::Save);
        QTRY_VERIFY(!ui.run.backend->dirty());
        QCOMPARE(ui.run.sent.load(), 0);
    }

    void uiSaveButtonEnabledWhenDirty()
    {
        Ui ui;
        QQuickItem *button = findItem(ui.window->contentItem(), "saveButton");
        QVERIFY(button);
        QVERIFY(!button->isEnabled());
        ui.run.backend->setColor(QColor("#123456"));
        QTRY_VERIFY(button->isEnabled());
        QMetaObject::invokeMethod(button, "clicked");
        QTRY_VERIFY(!ui.run.backend->dirty());
    }

    void uiNewProfileNameValidation()
    {
        Ui ui([](EditorRun &run) { seed(run, {"A", "B"}, "A"); });
        QQuickItem *button = findItem(ui.window->contentItem(), "newProfileButton");
        QVERIFY(button);
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, itemCentre(button));
        QQuickItem *field = nullptr;
        QTRY_VERIFY((field = findItem(ui.window->contentItem(), "profileNameField")) && field->isVisible());
        QQuickItem *ok = findItem(ui.window->contentItem(), "profileNameOk");
        QVERIFY(ok);
        QQuickItem *error = findItem(ui.window->contentItem(), "profileNameError");
        QVERIFY(error);
        QVERIFY(error->isVisible()); // empty name
        QVERIFY2(error->property("text").toString().contains("empty"), qPrintable(error->property("text").toString()));
        QVERIFY(!ok->isEnabled());
        field->setProperty("text", "B");
        QTRY_VERIFY(error->isVisible());
        QVERIFY(!ok->isEnabled());
        field->setProperty("text", "C");
        QTRY_VERIFY(!error->isVisible());
        QVERIFY(ok->isEnabled());
        QMetaObject::invokeMethod(ok, "clicked");
        QTRY_COMPARE(ui.run.backend->profileName(), QString("C"));
        QCOMPARE(ui.run.backend->profiles(), QStringList({"A", "B", "C"}));
    }

    void uiRenameAndDuplicateProfile()
    {
        Ui ui([](EditorRun &run) { seed(run, {"A"}, "A"); });
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, itemCentre(findItem(ui.window->contentItem(), "renameProfileButton")));
        QQuickItem *field = nullptr;
        QTRY_VERIFY((field = findItem(ui.window->contentItem(), "profileNameField")) && field->isVisible());
        QCOMPARE(field->property("text").toString(), QString("A"));
        field->setProperty("text", "Z");
        QQuickItem *ok = findItem(ui.window->contentItem(), "profileNameOk");
        QTRY_VERIFY(ok->isEnabled());
        QMetaObject::invokeMethod(ok, "clicked");
        QTRY_COMPARE(ui.run.backend->profileName(), QString("Z"));
        QTRY_VERIFY(!field->isVisible());
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, itemCentre(findItem(ui.window->contentItem(), "duplicateProfileButton")));
        QTRY_VERIFY(field->isVisible());
        QCOMPARE(field->property("text").toString(), QString("Z (copy)"));
        field->setProperty("text", "Z2");
        QTRY_VERIFY(ok->isEnabled());
        QMetaObject::invokeMethod(ok, "clicked");
        QTRY_COMPARE(ui.run.backend->profileName(), QString("Z2"));
        QCOMPARE(ui.run.backend->profiles(), QStringList({"Z", "Z2"}));
    }

    // Rename opens on the current name: no "already exists", and a change of case goes through.
    void uiRenameKeepsOwnName()
    {
        Ui ui([](EditorRun &run) { seed(run, {"tron", "B"}, "tron"); });
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, itemCentre(findItem(ui.window->contentItem(), "renameProfileButton")));
        QQuickItem *field = nullptr;
        QTRY_VERIFY((field = findItem(ui.window->contentItem(), "profileNameField")) && field->isVisible());
        QQuickItem *ok = findItem(ui.window->contentItem(), "profileNameOk");
        QQuickItem *error = findItem(ui.window->contentItem(), "profileNameError");
        QVERIFY(ok && error);
        QCOMPARE(field->property("text").toString(), QString("tron"));
        QVERIFY2(!error->isVisible(), qPrintable(error->property("text").toString()));
        QVERIFY(ok->isEnabled());
        field->setProperty("text", "b");
        QTRY_VERIFY(error->isVisible()); // another profile's name is still refused
        field->setProperty("text", "Tron");
        QTRY_VERIFY(!error->isVisible());
        QVERIFY(ok->isEnabled());
        QMetaObject::invokeMethod(ok, "clicked");
        QTRY_COMPARE(ui.run.backend->profileName(), QString("Tron"));
        QCOMPARE(ui.run.backend->profiles(), QStringList({"B", "Tron"}));
    }

    // Names come from the user: a tag in one is shown as typed, never as rich text.
    void uiNamesArePlainText()
    {
        Ui ui([](EditorRun &run) { seed(run, {"<b>P</b>"}, "<b>P</b>"); });
        QCOMPARE(ui.run.backend->profileName(), QString("<b>P</b>"));
        makeGroup(ui.run.backend, "<i>G</i>", {"KEY_Q"});
        ui.run.backend->select({"KEY_W"}, false); // shows the add-to-group box
        ui.run.backend->setColor(QColor("#123456"));
        QQuickItem *root = ui.window->contentItem();
        for (const char *name : {"profileBox", "addToGroupBox"}) {
            QQuickItem *box = findItem(root, name);
            QVERIFY2(box, name);
            QTRY_VERIFY(box->isVisible());
            QObject *popup = box->property("popup").value<QObject *>();
            QVERIFY(popup);
            QMetaObject::invokeMethod(popup, "open");
            QTRY_VERIFY(popup->property("opened").toBool());
            QTest::qWait(50);
            int shown = 0;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                if (item->inherits("QQuickText") && item->isVisible()) {
                    const QString text = item->property("text").toString();
                    if (text.contains("<b>") || text.contains("<i>")) {
                        ++shown;
                        QVERIFY2(item->property("textFormat").toInt() == 0, qPrintable(text)); // Text.PlainText
                    }
                }
                for (QQuickItem *child : item->childItems())
                    walk(child);
            };
            walk(root);
            QVERIFY2(shown >= 2, qPrintable(QString("%1: %2 names shown").arg(name).arg(shown)));
            QMetaObject::invokeMethod(popup, "close");
            QTRY_VERIFY(!popup->property("visible").toBool());
        }
        // The dialogs and the delete confirmation name the profile too.
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, itemCentre(findItem(root, "deleteProfileButton")));
        auto *dialog = ui.window->findChild<QObject *>("deleteProfileDialog");
        QTRY_VERIFY(dialog->property("opened").toBool());
        int shown = 0;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (item->inherits("QQuickText") && item->isVisible() && item->property("text").toString().contains("<b>")) {
                ++shown;
                QVERIFY2(item->property("textFormat").toInt() == 0, qPrintable(item->property("text").toString()));
            }
            for (QQuickItem *child : item->childItems())
                walk(child);
        };
        walk(root);
        QVERIFY(shown >= 1);
    }

    void uiDeleteProfileConfirms()
    {
        Ui ui([](EditorRun &run) { seed(run, {"A", "B"}, "A"); });
        ui.run.backend->linkTheme("tokyo-night");
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, itemCentre(findItem(ui.window->contentItem(), "deleteProfileButton")));
        auto *dialog = ui.window->findChild<QObject *>("deleteProfileDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(ui.run.backend->profiles().size(), 2);
        QMetaObject::invokeMethod(dialog, "accepted");
        QTRY_COMPARE(ui.run.backend->profiles(), QStringList({"B"}));
    }

    void uiThemeBoxLinks()
    {
        Ui ui([](EditorRun &run) {
            seed(run, {"A"}, "A");
            QDir().mkpath(run.paths.stockThemes + "/tron");
        });
        QQuickItem *box = findItem(ui.window->contentItem(), "themeBox");
        QVERIFY(box);
        QVERIFY(box->property("count").toInt() >= 2); // none + tron
        int index = -1;
        for (int i = 0; i < box->property("count").toInt(); ++i)
            if (callText(box, i).toString() == "tron")
                index = i;
        QVERIFY(index > 0);
        box->setProperty("currentIndex", index);
        QMetaObject::invokeMethod(box, "activated", Q_ARG(int, index));
        QTRY_COMPARE(ui.run.backend->linkedTheme(), QString("tron"));
        QCOMPARE(box->property("currentIndex").toInt(), index);
        // Back to none.
        QMetaObject::invokeMethod(box, "activated", Q_ARG(int, 0));
        QTRY_COMPARE(ui.run.backend->linkedTheme(), QString());
    }

    void uiThemeLinkedElsewhereNotes()
    {
        Ui ui([](EditorRun &run) {
            seed(run, {"A", "B"}, "A");
            QDir().mkpath(run.paths.stockThemes + "/tron");
        });
        QCOMPARE(ui.run.backend->linkTheme("tron"), QString());
        QQuickItem *box = findItem(ui.window->contentItem(), "themeBox");
        ui.run.backend->openProfile("B");
        int index = -1;
        for (int i = 0; i < box->property("count").toInt(); ++i)
            if (callText(box, i).toString() == "tron")
                index = i;
        QMetaObject::invokeMethod(box, "activated", Q_ARG(int, index));
        QQuickItem *note = nullptr;
        QTRY_VERIFY((note = findItem(ui.window->contentItem(), "themeNote")) && note->isVisible());
        QVERIFY2(note->property("text").toString().contains("A"), qPrintable(note->property("text").toString()));
    }

    // Changes are saved by Save (Ctrl+S) or Apply: unsaved is what the status line reports.
    void uiStatusSaysUnsaved()
    {
        Ui ui;
        QQuickItem *status = findItem(ui.window->contentItem(), "statusLabel");
        QVERIFY(status);
        QCOMPARE(status->property("text").toString(), QString());
        ui.run.backend->setColor(QColor("#123456"));
        QTRY_COMPARE(status->property("text").toString(), QString("Unsaved changes"));
        QVERIFY(ui.run.backend->save());
        QTRY_COMPARE(status->property("text").toString(), QString());
    }

    // Each help line pairs a key with what it does; Ctrl+S is listed.
    void uiHelpListsShortcuts()
    {
        Ui ui;
        ui.window->setProperty("helpVisible", true);
        QQuickItem *root = ui.window->contentItem();
        QMap<QString, QString> help;
        for (int i = 0;; ++i) {
            QQuickItem *key = findItem(root, QString("helpKey%1").arg(i));
            QQuickItem *text = findItem(root, QString("helpText%1").arg(i));
            if (!key)
                break;
            QVERIFY2(text, qPrintable(key->property("text").toString()));
            help.insert(key->property("text").toString(), text->property("text").toString());
        }
        QCOMPARE(help.value("Click"), QString("Select a key"));
        QCOMPARE(help.value("Space"), QString("Apply to the keyboard (nothing is sent before)"));
        QCOMPARE(help.value("Q"), QString("Quit"));
        QCOMPARE(help.value("Ctrl+S"), QString("Save without sending"));
    }

    void uiQuitSaveSaves()
    {
        Ui ui;
        ui.run.backend->setColor(QColor("#123456"));
        QMetaObject::invokeMethod(ui.window, "requestQuit");
        auto *dialog = ui.window->findChild<QObject *>("quitDialog");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QMetaObject::invokeMethod(dialog, "accepted");
        QTRY_VERIFY(!ui.window->isVisible());
        QVERIFY(!ui.run.backend->dirty());
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
    // The middle of the ring's band, from the ring's own geometry.
    static QPointF ringPoint(QQuickItem *ring, double degrees)
    {
        const double a = qDegreesToRadians(degrees);
        const double c = ring->property("centre").toDouble();
        const double r = (ring->property("innerRadius").toDouble() + ring->property("outerRadius").toDouble()) / 2;
        return ring->mapToScene(QPointF(c + r * std::cos(a), c - r * std::sin(a)));
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
        ui.run.backend->setColor(QColor("#000000"));
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

    // Step durations read in seconds (1 controller unit = 10 ms, the preview's assumption).
    // Items made by a Repeater are not QObject children of the window: walk the visual tree.
    static QQuickItem *findItem(QQuickItem *root, const QString &name)
    {
        if (root->objectName() == name)
            return root;
        for (QQuickItem *child : root->childItems())
            if (QQuickItem *found = findItem(child, name))
                return found;
        return nullptr;
    }

    void uiStepDurationInSeconds()
    {
        Ui ui;
        ui.run.backend->setBaseType("wave");
        QQuickItem *box = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((box = findItem(ui.window->contentItem(), "stepDuration")) && box->isVisible(), 2000);
        ui.run.backend->setStepDuration(0, 55);
        // The step rows may be rebuilt: look the field up again.
        QTRY_VERIFY((box = findItem(ui.window->contentItem(), "stepDuration")) && box->property("value").toInt() == 55);
        QVERIFY2(QRegularExpression("^0[.,]55 s$").match(box->property("displayText").toString()).hasMatch(),
                 qPrintable(box->property("displayText").toString()));
    }

    // Holding + repeats the step; the whole hold is one undo step.
    void uiStepDurationHoldIsOneUndoStep()
    {
        Ui ui;
        ui.run.backend->setBaseType("wave");
        QQuickItem *box = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((box = findItem(ui.window->contentItem(), "stepDuration")) && box->isVisible(), 2000);
        QTest::qWait(300); // let the layout settle before aiming at the button
        QTRY_VERIFY((box = findItem(ui.window->contentItem(), "stepDuration")) != nullptr);
        const int before = box->property("value").toInt();
        auto *up = box->property("up").value<QObject *>()->property("indicator").value<QQuickItem *>();
        QVERIFY(up);
        const QPoint centre = up->mapToScene(QPointF(up->width() / 2, up->height() / 2)).toPoint();
        const QPointer<QQuickItem> held = box;
        QTest::mousePress(ui.window, Qt::LeftButton, {}, centre);
        QTest::qWait(1200);
        QTest::mouseRelease(ui.window, Qt::LeftButton, {}, centre);
        QVERIFY2(held, "the step row was rebuilt while + was held");
        QVERIFY(box->property("value").toInt() > before + 2);
        ui.run.backend->undo();
        QTRY_COMPARE(box->property("value").toInt(), before);
    }

    // Scene position of the centre of a keyboard key, from the layout units of backend.keys.
    static QPoint keyCentre(QQuickWindow *window, Backend *backend, const QString &id)
    {
        QQuickItem *view = findItem(window->contentItem(), "keyboardView");
        if (!view)
            return {};
        for (const QVariant &v : backend->keys()) {
            const QVariantMap k = v.toMap();
            if (k.value("id").toString() != id)
                continue;
            const double s = view->property("scale").toDouble();
            const double x = view->property("offsetX").toDouble() +
                             ((k.value("left").toDouble() + k.value("right").toDouble()) / 2 - view->property("minX").toDouble()) * s;
            const double y = view->property("offsetY").toDouble() +
                             ((k.value("top").toDouble() + k.value("bottom").toDouble()) / 2 - view->property("minY").toDouble()) * s;
            return view->mapToScene(QPointF(x, y)).toPoint();
        }
        return {};
    }

    static QPoint itemCentre(QQuickItem *item)
    {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    }

    void uiClickKeyOpensGroup()
    {
        Ui ui;
        makeGroup(ui.run.backend, "ZQSD", {"KEY_W", "KEY_A", "KEY_S", "KEY_D"});
        ui.run.backend->openBase();
        QTRY_VERIFY(!keyCentre(ui.window, ui.run.backend, "KEY_A").isNull());
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, keyCentre(ui.window, ui.run.backend, "KEY_A"));
        QQuickItem *name = nullptr;
        QTRY_VERIFY((name = findItem(ui.window->contentItem(), "groupNameField")) && name->isVisible());
        QTRY_COMPARE(name->property("text").toString(), QString("ZQSD"));
        QQuickItem *behavior = findItem(ui.window->contentItem(), "behaviorBox");
        QVERIFY(behavior && behavior->isVisible());
    }

    void uiSelectionPanel()
    {
        Ui ui;
        QQuickItem *list = findItem(ui.window->contentItem(), "groupList");
        QVERIFY(list);
        const int before = list->property("count").toInt();
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, keyCentre(ui.window, ui.run.backend, "KEY_ESC"));
        QQuickItem *name = nullptr;
        QTRY_VERIFY((name = findItem(ui.window->contentItem(), "newGroupName")) && name->isVisible());
        QCOMPARE(name->property("text").toString(), ui.run.backend->defaultGroupName());
        QQuickItem *create = findItem(ui.window->contentItem(), "createGroupButton");
        QVERIFY(create && create->isVisible());
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, itemCentre(create));
        QTRY_COMPARE(list->property("count").toInt(), before + 1);
    }

    // A refused creation says why, next to the field.
    void uiCreateGroupError()
    {
        Ui ui;
        makeGroup(ui.run.backend, "Mix", {"KEY_Q"});
        ui.run.backend->select({"KEY_ESC"}, false);
        QQuickItem *name = nullptr;
        QTRY_VERIFY((name = findItem(ui.window->contentItem(), "newGroupName")) && name->isVisible());
        name->setProperty("text", "Mix");
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, itemCentre(findItem(ui.window->contentItem(), "createGroupButton")));
        QQuickItem *problem = nullptr;
        QTRY_VERIFY((problem = findItem(ui.window->contentItem(), "newGroupError")) && problem->isVisible());
        QVERIFY(problem->property("text").toString().contains("Mix"));
    }

    void uiGroupRowOpens()
    {
        Ui ui;
        makeGroup(ui.run.backend, "One", {"KEY_Q"});
        makeGroup(ui.run.backend, "Two", {"KEY_W"});
        ui.run.backend->openBase();
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = findItem(ui.window->contentItem(), "groupRow1")) && row->isVisible());
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, itemCentre(row));
        QTRY_COMPARE(ui.run.backend->currentGroup(), 1);
    }

    void uiAnimatedCount()
    {
        Ui ui;
        makeGroup(ui.run.backend, "One", {"KEY_Q"});
        QVERIFY(ui.run.backend->setBehavior("pulse").isEmpty());
        makeGroup(ui.run.backend, "Two", {"KEY_W"});
        QVERIFY(ui.run.backend->setBehavior("pulse").isEmpty());
        QQuickItem *label = findItem(ui.window->contentItem(), "animatedCount");
        QVERIFY(label);
        QTRY_VERIFY(label->property("text").toString().contains("2 / 6"));
    }

    // Whether an item lies fully inside another one's visible area.
    static bool within(QQuickItem *item, QQuickItem *viewport)
    {
        if (!item || !viewport || !item->isVisible())
            return false;
        const QRectF r = item->mapRectToItem(viewport, QRectF(0, 0, item->width(), item->height()));
        return r.top() >= -0.5 && r.bottom() <= viewport->height() + 0.5 && r.left() >= -0.5 && r.right() <= viewport->width() + 0.5;
    }

    // At the default 1280x640 the whole left column is on screen without scrolling.
    void uiLeftColumnFitsDefaultWindow()
    {
        Ui ui;
        QCOMPARE(ui.window->size(), QSize(1280, 640));
        makeGroup(ui.run.backend, "One", {"KEY_Q"});
        makeGroup(ui.run.backend, "Two", {"KEY_W"});
        ui.run.backend->openBase();
        QTest::qWait(300);
        QQuickItem *root = ui.window->contentItem();
        QQuickItem *scroll = findItem(root, "leftScroll");
        QQuickItem *list = findItem(root, "groupList");
        QVERIFY(scroll && list);
        for (const char *name : {"baseRow", "createFromSelectionButton", "animatedCount", "reactiveRow", "selectKeysHint"})
            QVERIFY2(within(findItem(root, name), scroll), name);
        for (const char *name : {"groupRow0", "groupRow1"}) {
            QVERIFY2(within(findItem(root, name), scroll), name);
            QVERIFY2(within(findItem(root, name), list), name);
        }
        QQuickItem *bar = findItem(root, "actionBar");
        QVERIFY(within(bar, root));
    }

    // At the default 1280x640 the whole color picker is on screen without scrolling: ring,
    // intensity, hex field and the three RGB sliders.
    void uiPickerVisibleDefaultWindow()
    {
        Ui ui;
        QCOMPARE(ui.window->size(), QSize(1280, 640));
        makeGroup(ui.run.backend, "One", {"KEY_Q"});
        QCOMPARE(ui.run.backend->currentKind(), QString("group"));
        QTest::qWait(300);
        QQuickItem *root = ui.window->contentItem();
        QQuickItem *scroll = findItem(root, "rightScroll");
        QVERIFY(scroll);
        for (const char *name : {"hueRing", "intensitySlider", "hexField", "rgbSlider0", "rgbSlider1", "rgbSlider2"}) {
            QQuickItem *item = findItem(root, name);
            QVERIFY2(item, name);
            const QRectF r = item->mapRectToItem(scroll, QRectF(0, 0, item->width(), item->height()));
            QVERIFY2(within(item, scroll), qPrintable(QString("%1 at %2,%3 %4×%5, panel %6×%7").arg(name)
                                                          .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height())
                                                          .arg(scroll->width()).arg(scroll->height())));
        }
    }

    // At the minimum size the window still works: action bar on screen, no QML warning.
    void uiMinimumWindowSmoke()
    {
        QTest::failOnWarning(QRegularExpression("."));
        Ui ui;
        makeGroup(ui.run.backend, "One", {"KEY_Q"});
        ui.window->resize(900, 480);
        QTest::qWait(300);
        QCOMPARE(ui.window->size(), QSize(900, 480));
        QQuickItem *root = ui.window->contentItem();
        QVERIFY(within(findItem(root, "actionBar"), root));
        for (const char *name : {"applyButton", "saveButton", "undoButton", "redoButton"})
            QVERIFY2(within(findItem(root, name), root), name);
        QVERIFY(findItem(root, "hexField")->isVisible());
        ui.run.backend->openBase();
        QTest::qWait(100);
    }

    void uiBaseRowShowsType()
    {
        Ui ui;
        makeGroup(ui.run.backend, "One", {"KEY_Q"}); // leaves the group open, not the base
        QCOMPARE(ui.run.backend->currentKind(), QString("group"));
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = findItem(ui.window->contentItem(), "baseRow")) != nullptr);
        QVERIFY2(QRegularExpression("static|fixe", QRegularExpression::CaseInsensitiveOption)
                     .match(row->property("text").toString()).hasMatch(), qPrintable(row->property("text").toString()));
        ui.run.backend->setBaseType("wave");
        QTRY_VERIFY2(QRegularExpression("wave|vague", QRegularExpression::CaseInsensitiveOption)
                         .match(row->property("text").toString()).hasMatch(), qPrintable(row->property("text").toString()));
    }

    void uiRenameGroup()
    {
        Ui ui;
        makeGroup(ui.run.backend, "One", {"KEY_Q"});
        makeGroup(ui.run.backend, "Two", {"KEY_W"});
        QQuickItem *field = nullptr;
        QTRY_VERIFY((field = findItem(ui.window->contentItem(), "groupNameField")) && field->isVisible());
        field->setProperty("text", "Renamed");
        QMetaObject::invokeMethod(field, "editingFinished");
        QTRY_COMPARE(ui.run.backend->groups().at(1).toMap().value("name").toString(), QString("Renamed"));
        QQuickItem *error = findItem(ui.window->contentItem(), "groupNameError");
        QVERIFY(error && !error->isVisible());
        // A name already taken is refused, and the reason shows under the field.
        field->setProperty("text", "One");
        QMetaObject::invokeMethod(field, "editingFinished");
        QTRY_VERIFY(error->isVisible());
        QVERIFY2(error->property("text").toString().contains("One"), qPrintable(error->property("text").toString()));
        QCOMPARE(ui.run.backend->groups().at(1).toMap().value("name").toString(), QString("Renamed"));
    }

    void uiBehaviorRefusalShowsError()
    {
        Ui ui;
        const QStringList keys = {"KEY_Q", "KEY_W", "KEY_E", "KEY_R", "KEY_T", "KEY_Y", "KEY_U"};
        for (int i = 0; i < 7; ++i) {
            makeGroup(ui.run.backend, QString("G%1").arg(i), {keys.at(i)});
            if (i < 6)
                QVERIFY(ui.run.backend->setBehavior("pulse").isEmpty());
        }
        QQuickItem *box = nullptr;
        QTRY_VERIFY((box = findItem(ui.window->contentItem(), "behaviorBox")) && box->isVisible());
        box->setProperty("currentIndex", 1); // Pulse
        QMetaObject::invokeMethod(box, "activated", Q_ARG(int, 1));
        QQuickItem *error = findItem(ui.window->contentItem(), "behaviorError");
        QTRY_VERIFY(error && error->isVisible());
        QVERIFY(error->property("text").toString().contains("6"));
        QCOMPARE(ui.run.backend->current().value("behavior").toString(), QString("static"));
        // The box goes back to what the group really is.
        QTRY_COMPARE(box->property("currentIndex").toInt(), 0);
    }

    void uiRemoveSelectedKeys()
    {
        Ui ui;
        makeGroup(ui.run.backend, "Trio", {"KEY_Q", "KEY_W", "KEY_E"});
        ui.run.backend->select({"KEY_Q"}, false);
        QQuickItem *button = nullptr;
        QTRY_VERIFY((button = findItem(ui.window->contentItem(), "removeKeysButton")) && button->isVisible() && button->isEnabled());
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, itemCentre(button));
        QTRY_COMPARE(ui.run.backend->groups().at(0).toMap().value("count").toInt(), 2);
    }

    void uiDeleteGroup()
    {
        Ui ui;
        makeGroup(ui.run.backend, "Gone", {"KEY_Q"});
        QQuickItem *button = nullptr;
        QTRY_VERIFY((button = findItem(ui.window->contentItem(), "deleteGroupButton")) && button->isVisible());
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, itemCentre(button));
        QTRY_VERIFY(ui.run.backend->groups().isEmpty());
        QCOMPARE(ui.run.backend->currentKind(), QString("base"));
    }

    void uiAddToGroupBox()
    {
        Ui ui;
        makeGroup(ui.run.backend, "A", {"KEY_Q"});
        ui.run.backend->openBase();
        ui.run.backend->select({"KEY_W"}, false);
        QQuickItem *box = nullptr;
        QTRY_VERIFY((box = findItem(ui.window->contentItem(), "addToGroupBox")) && box->isVisible());
        QMetaObject::invokeMethod(box, "activated", Q_ARG(int, 0));
        QTRY_COMPARE(ui.run.backend->groups().at(0).toMap().value("count").toInt(), 2);
    }

    void uiEscapeReturnsToBase()
    {
        Ui ui;
        makeGroup(ui.run.backend, "A", {"KEY_Q"});
        ui.run.backend->select({"KEY_Q", "KEY_W"}, false);
        QTest::keyClick(ui.window, Qt::Key_Escape);
        QTRY_COMPARE(ui.run.backend->currentKind(), QString("base"));
        QVERIFY(ui.run.backend->selection().isEmpty());
    }

    void uiGroupKeysHighlighted()
    {
        Ui ui;
        makeGroup(ui.run.backend, "A", {"KEY_Q", "KEY_W"});
        QQuickItem *q = nullptr, *e = nullptr;
        QTRY_VERIFY((q = findItem(ui.window->contentItem(), "key_KEY_Q")) != nullptr);
        e = findItem(ui.window->contentItem(), "key_KEY_E");
        QVERIFY(e);
        QTRY_VERIFY(q->property("inGroup").toBool());
        QVERIFY(!e->property("inGroup").toBool());
        ui.run.backend->openBase();
        QTRY_VERIFY(!q->property("inGroup").toBool());
    }

    void uiSelectKeysHint()
    {
        Ui ui;
        QQuickItem *hint = findItem(ui.window->contentItem(), "selectKeysHint");
        QVERIFY(hint);
        QTRY_VERIFY(hint->isVisible());
        ui.run.backend->selectGroup("wasd");
        QTRY_VERIFY(!hint->isVisible());
    }

    static QQuickItem *picker(QQuickWindow *window)
    {
        auto *ring = window->findChild<QQuickItem *>("hueRing");
        return ring ? ring->parentItem() : nullptr;
    }

    // The hex field accepts "#rrggbb" or "rrggbb" with spaces; anything else reverts.
    void uiHexFieldParses()
    {
        Ui ui;
        QQuickItem *pk = picker(ui.window);
        QVERIFY(pk);
        ui.run.backend->setColor(QColor("#102030"));
        QQuickItem *hex = findItem(ui.window->contentItem(), "hexField");
        QVERIFY(hex);
        QTRY_COMPARE(hex->property("text").toString(), QString("#102030"));
        hex->forceActiveFocus();
        hex->setProperty("text", " 00e5ff ");
        QTest::keyClick(ui.window, Qt::Key_Return);
        QCOMPARE(pk->property("color").value<QColor>(), QColor("#00e5ff"));
        QTRY_COMPARE(hex->property("text").toString(), QString("#00E5FF"));
        hex->forceActiveFocus();
        hex->setProperty("text", "#12345");
        QTest::keyClick(ui.window, Qt::Key_Return);
        QCOMPARE(pk->property("color").value<QColor>(), QColor("#00e5ff"));
        QCOMPARE(hex->property("text").toString(), QString("#00E5FF"));
    }

    // The picker must follow the open group (a pick used to break its binding for good).
    void uiPickerFollowsGroupSwitch()
    {
        Ui ui;
        Backend *b = ui.run.backend;
        b->select({"KEY_A"}, false);
        QCOMPARE(b->createGroup("A"), QString());
        b->setColor(QColor("#0000ff"));
        b->select({"KEY_B"}, false);
        QCOMPARE(b->createGroup("B"), QString());
        b->setColor(QColor("#00ff00"));
        b->openGroup(0);
        QQuickItem *hex = findItem(ui.window->contentItem(), "hexField");
        QQuickItem *slider0 = findItem(ui.window->contentItem(), "rgbSlider0");
        QQuickItem *slider2 = findItem(ui.window->contentItem(), "rgbSlider2");
        QVERIFY(hex && slider0 && slider2);
        QTRY_COMPARE(hex->property("text").toString(), QString("#0000FF"));
        // Change A through the hex field.
        hex->forceActiveFocus();
        hex->setProperty("text", "ff8000");
        QTest::keyClick(ui.window, Qt::Key_Return);
        QTRY_COMPARE(hex->property("text").toString(), QString("#FF8000"));
        QCOMPARE(slider0->property("value").toInt(), 255);
        b->openGroup(1);
        QTRY_COMPARE(hex->property("text").toString(), QString("#00FF00"));
        QCOMPARE(slider0->property("value").toInt(), 0);
        QCOMPARE(slider2->property("value").toInt(), 0);
        b->openGroup(0);
        QTRY_COMPARE(hex->property("text").toString(), QString("#FF8000"));
        QCOMPARE(slider0->property("value").toInt(), 255);
        QCOMPARE(findItem(ui.window->contentItem(), "rgbValue1")->property("text").toString(), QString("128"));
    }

    static int groupIndex(Backend *backend, const QString &name)
    {
        const auto &groups = backend->document().groups;
        for (int i = 0; i < groups.size(); ++i)
            if (groups[i].name == name)
                return i;
        return -1;
    }

    // The picker edits a step that exists: after moving to another animated group, and after
    // the selected step is removed.
    void uiPickerStepFollowsGroup()
    {
        Ui ui;
        Backend *b = ui.run.backend;
        makeGroup(b, "A", {"KEY_Q"});
        QCOMPARE(b->setBehavior("pulse"), QString());
        b->addStep();
        b->addStep();
        makeGroup(b, "B", {"KEY_W"});
        QCOMPARE(b->setBehavior("pulse"), QString());
        const int a = groupIndex(b, "A"), bi = groupIndex(b, "B");
        b->openGroup(a);
        QCOMPARE(b->document().groups[a].steps.size(), 3);
        QQuickItem *panel = findItem(ui.window->contentItem(), "effectPanel");
        QQuickItem *pk = picker(ui.window);
        QVERIFY(panel && pk);
        panel->setProperty("stepIndex", 2); // the third step's swatch, clicked
        b->openGroup(bi);
        QTRY_COMPARE(b->currentGroup(), bi);
        QCOMPARE(b->document().groups[bi].steps.size(), 1);
        QMetaObject::invokeMethod(pk, "picked", Q_ARG(QColor, QColor("#ff0000")));
        QCOMPARE(b->document().groups[bi].steps[0].color, QColor("#ff0000"));

        // Remove the selected last step, then pick: the new last step takes the color.
        b->openGroup(a);
        panel->setProperty("stepIndex", 2);
        b->removeStep(2);
        QCOMPARE(b->document().groups[a].steps.size(), 2);
        QMetaObject::invokeMethod(pk, "picked", Q_ARG(QColor, QColor("#00ff00")));
        QCOMPARE(b->document().groups[a].steps[1].color, QColor("#00ff00"));
        QCOMPARE(panel->property("stepIndex").toInt(), 1);
    }

    void uiRgbSliderDragIsOneUndoStep()
    {
        Ui ui;
        QTest::qWait(200);
        QQuickItem *slider = findItem(ui.window->contentItem(), "rgbSlider0");
        QVERIFY(slider && slider->isVisible());
        QVERIFY(!ui.run.backend->canUndo());
        auto at = [&](double f) { return slider->mapToScene(QPointF(slider->width() * f, slider->height() / 2)).toPoint(); };
        QTest::mousePress(ui.window, Qt::LeftButton, {}, at(0.1));
        for (double f = 0.2; f <= 0.8; f += 0.1)
            QTest::mouseMove(ui.window, at(f));
        QTest::mouseRelease(ui.window, Qt::LeftButton, {}, at(0.8));
        QVERIFY(ui.run.backend->canUndo());
        ui.run.backend->undo();
        QVERIFY(!ui.run.backend->canUndo());
    }

    void uiRgbValueKeys()
    {
        Ui ui;
        QTest::qWait(200);
        QQuickItem *pk = picker(ui.window);
        QVERIFY(pk);
        ui.run.backend->setColor(QColor(10, 100, 30));
        QQuickItem *value = findItem(ui.window->contentItem(), "rgbValue1");
        QVERIFY(value && value->isVisible());
        QTRY_COMPARE(value->property("text").toString(), QString("100"));
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, value->mapToScene(QPointF(value->width() / 2, value->height() / 2)).toPoint());
        QQuickItem *edit = findItem(ui.window->contentItem(), "rgbEdit1");
        QVERIFY(edit && edit->isVisible());
        QTRY_VERIFY(edit->hasActiveFocus());
        QTest::keyClick(ui.window, Qt::Key_Up);
        QCOMPARE(pk->property("color").value<QColor>().green(), 101);
        QTest::keyClick(ui.window, Qt::Key_Up, Qt::ShiftModifier);
        QCOMPARE(pk->property("color").value<QColor>().green(), 111);
        QTest::keyClick(ui.window, Qt::Key_Escape);
        QCOMPARE(pk->property("color").value<QColor>().green(), 100);
        QTRY_VERIFY(!findItem(ui.window->contentItem(), "rgbEdit1") || !findItem(ui.window->contentItem(), "rgbEdit1")->isVisible());
        QTest::mouseClick(ui.window, Qt::LeftButton, {}, value->mapToScene(QPointF(value->width() / 2, value->height() / 2)).toPoint());
        edit = findItem(ui.window->contentItem(), "rgbEdit1");
        QTRY_VERIFY(edit && edit->hasActiveFocus());
        QTest::keyClick(ui.window, Qt::Key_Down);
        QTest::keyClick(ui.window, Qt::Key_Return);
        QCOMPARE(pk->property("color").value<QColor>().green(), 99);
        QTRY_COMPARE(value->property("text").toString(), QString("99"));
    }

    void uiRgbLabelsTranslated()
    {
        Ui ui;
        QCoreApplication::installTranslator(&ui.translator);
        QObject::connect(&ui.translator, &Translator::changed, &ui.engine, &QQmlEngine::retranslate);
        auto labels = [&] {
            QStringList l;
            for (int i = 0; i < 3; ++i) {
                QQuickItem *it = findItem(ui.window->contentItem(), QString("rgbLabel%1").arg(i));
                l << (it ? it->property("text").toString() : QString("?"));
            }
            return l;
        };
        QCOMPARE(labels(), (QStringList{"R", "G", "B"}));
        write(ui.langDir.path() + "/language", "fr\n");
        QTRY_COMPARE(labels(), (QStringList{"R", "V", "B"}));
        QCoreApplication::removeTranslator(&ui.translator);
    }

    // Apart from the hex field, no boxed TextField is visible in the picker while idle.
    void uiRgbNoBoxedFields()
    {
        Ui ui;
        QQuickItem *pk = picker(ui.window);
        QVERIFY(pk);
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (item->isVisible() && item->inherits("QQuickTextField")) {
                QVERIFY2(item->objectName() == "hexField", qPrintable(item->objectName()));
                auto *bg = item->property("background").value<QQuickItem *>();
                QVERIFY(!bg || bg->property("color").value<QColor>().alpha() == 0
                        || !bg->property("visible").toBool() || bg->width() == 0);
            }
            for (QQuickItem *child : item->childItems())
                walk(child);
        };
        walk(pk);
        QVERIFY(findItem(ui.window->contentItem(), "hexField"));
    }

    // The saved-color slots say what they are: a title, a hint while they are all empty,
    // and no cryptic "+" in empty slots.
    void uiSavedColorsExplained()
    {
        Ui ui;
        QQuickItem *pk = picker(ui.window);
        QVERIFY(pk);
        QQuickItem *title = findItem(pk, "savedColorsTitle");
        QVERIFY(title && title->isVisible());
        QQuickItem *hint = findItem(pk, "savedColorsHint");
        QVERIFY(hint);
        QTRY_VERIFY(hint->isVisible());
        std::function<void(QQuickItem *)> noPlus = [&](QQuickItem *item) {
            if (item->isVisible() && item->property("text").toString() == "+")
                QFAIL(qPrintable(QString("a visible \"+\" in %1").arg(item->objectName())));
            for (QQuickItem *child : item->childItems())
                noPlus(child);
        };
        noPlus(pk);
        ui.run.backend->setSavedColor(0, QColor("#00E5FF"));
        QTRY_VERIFY(!hint->isVisible());
        QVERIFY(title->isVisible());
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
        addProfile(dir, "Tokyo", "#7aa2f7", "tokyo-night");
        CliRun run(dir, paths);
        run.binder.bindOk = false;
        run.binder.bindError = "SteelSeries KLC (1038:113a) not found";
        QCOMPARE(run.exec({"apply", "--theme"}), 2);
        QVERIFY(run.err.contains("-71"));
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

    void layoutPowerKey()
    {
        const Key *power = Layout::gs76().byId("KEY_POWER");
        QVERIFY(power);
        QCOMPARE(power->hid, quint8(0x66));
        QVERIFY(power->visible);
        QVERIFY(Layout::gs76().group("all").contains("KEY_POWER"));
        QCOMPARE(power->cx, 4294);
        QCOMPARE(power->cy, 1075);
    }

    // Key positions in the controller's units, from SteelSeries GG's GE76 table.
    void layoutControllerPositions()
    {
        const Layout &l = Layout::gs76();
        QCOMPARE(l.byId("KEY_ESC")->cx, 0);
        QCOMPARE(l.byId("KEY_ESC")->cy, 1075);
        QCOMPARE(l.byId("KEY_SPACE")->cx, 1738);
        QCOMPARE(l.byId("KEY_SPACE")->cy, 0);
        for (const Key &key : l.keys())
            QVERIFY2(key.cx >= 0 && key.cy >= 0, qPrintable(key.id));
    }

private:
    static void expectSamePackets(const Profile &profile)
    {
        const BuildResult want = buildPackets(profile, Layout::gs76());
        const BuildResult got = buildPackets(lighting(fromLighting(profile, "t")), Layout::gs76());
        QVERIFY(want.errors.isEmpty());
        QCOMPARE(got.errors, want.errors);
        QCOMPARE(got.packets.size(), want.packets.size());
        for (int i = 0; i < want.packets.size(); ++i) {
            QCOMPARE(int(got.packets[i].kind), int(want.packets[i].kind));
            QCOMPARE(got.packets[i].data, want.packets[i].data);
        }
    }


    // An editor on a fake theme whose sends are counted instead of reaching a keyboard.
    struct EditorRun {
        explicit EditorRun(const QByteArray &effect = {}) {
            paths.stateTheme = dir.path() + "/state/current/theme";
            paths.themeName = dir.path() + "/state/current/theme.name";
            paths.userThemes = dir.path() + "/config/themes";
            paths.stockThemes = dir.path() + "/stock/themes";
            QDir().mkpath(paths.stateTheme);
            QFile name(paths.themeName);
            name.open(QIODevice::WriteOnly);
            name.write("tokyo-night\n");
            name.close();
            if (!effect.isEmpty()) {
                // The library is migrated on start: the theme's effect becomes its profile.
                QDir().mkpath(paths.stockThemes + "/tokyo-night");
                QFile file(paths.stockThemes + "/tokyo-night/keyboard.effect");
                file.open(QIODevice::WriteOnly);
                file.write(effect + "\n");
            }
            backend = new Backend(paths, dir.path() + "/klc", KeyLabels::fromRmlvo("fr", ""),
                                  [this](const QList<Packet> &) { ++sent; while (hold) QThread::msleep(5); return result; },
                                  dir.path() + "/colors.json");
        }
        ~EditorRun() { hold = false; delete backend; } // release a blocked sender first
        QTemporaryDir dir;
        ThemePaths paths;
        std::atomic<int> sent{0};
        std::atomic<bool> hold{false}; // the fake keyboard waits while set
        ApplyResult result; // what the fake keyboard answers
        Backend *backend = nullptr;
    };

    // A library holding the given profiles (applied = the last argument when not empty), then
    // a fresh editor over it.
    static void seed(EditorRun &run, const QStringList &names, const QString &applied = {})
    {
        // Whatever the first start made (migration) goes.
        QDir(run.dir.path() + "/klc").removeRecursively();
        Library library(run.dir.path() + "/klc");
        for (const QString &name : names) {
            Document doc;
            doc.name = name;
            QString error;
            QVERIFY2(library.save(doc, &error), qPrintable(error));
        }
        if (!applied.isEmpty())
            library.setApplied(applied);
        delete run.backend;
        run.backend = new Backend(run.paths, run.dir.path() + "/klc", KeyLabels::fromRmlvo("fr", ""),
                                  [&run](const QList<Packet> &) { ++run.sent; while (run.hold) QThread::msleep(5); return run.result; },
                                  run.dir.path() + "/colors.json");
    }

    // Runs the command line against a fake theme and a fake keyboard.
    struct CliRun {
        CliRun(const QTemporaryDir &dir, const ThemePaths &paths)
            : transport(log), binder(log),
              device(transport, binder, dir.path(),
                     [this](int ms) { log.sleeps << ms; log.now += ms; if (onSleep) onSleep(ms); },
                     [this] { return log.now; }),
              outStream(&out), errStream(&err), env{paths, dir.path() + "/klc", &device, &outStream, &errStream} {}
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
        paths.stockThemes = dir.path() + "/stock/themes";
        QDir().mkpath(paths.stateTheme);
        write(paths.themeName, name.toUtf8() + "\n");
        return paths;
    }

    enum FixtureMode { AllBytes, SkipTransitions };

    static void compareWithFixture(const BuildResult &result, const QString &name, FixtureMode mode = AllBytes)
    {
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join("; ")));
        const auto expected = fixture(name);
        QCOMPARE(result.packets.size(), expected.size());
        for (int i = 0; i < expected.size(); ++i) {
            QCOMPARE(QChar(result.packets[i].kind == Packet::Feature ? 'F' : 'O'), expected[i].first);
            const QByteArray got = result.packets[i].data;
            QByteArray want = expected[i].second;
            // An effect packet's transition table (0x02..0x81) and total duration (0x98..0x99).
            if (mode == SkipTransitions && quint8(want.at(0)) == 0x0b && got.size() == want.size()) {
                want.replace(0x02, 0x80, got.mid(0x02, 0x80));
                want.replace(0x98, 2, got.mid(0x98, 2));
            }
            // Key packets: msi-perkeyrgb leaves a code-0 filler element and counts 11 keys
            // where we send the power key as a twelfth. Compare the header, every real key
            // element but power, and the trailer.
            if (quint8(want.at(0)) == 0x0e && got.size() == want.size()) {
                QCOMPARE(keyElements(got, true), keyElements(want, false));
                continue;
            }
            if (got != want) {
                int at = 0;
                while (at < qMin(got.size(), want.size()) && got[at] == want[at])
                    ++at;
                QFAIL(qPrintable(QString("packet %1 differs at byte 0x%2: got %3, want %4")
                                     .arg(i).arg(at, 0, 16)
                                     .arg(QString(got.mid(at, 12).toHex(' ')), QString(want.mid(at, 12).toHex(' ')))));
            }
        }
    }

    // A key packet without its count byte: header, the elements of real keys (the power key
    // dropped when asked), trailer.
    static QByteArray keyElements(const QByteArray &packet, bool dropPower)
    {
        QByteArray out = packet.left(2) + packet.mid(3, 1);
        for (int i = 0; i < 42; ++i) {
            const QByteArray element = packet.mid(4 + i * 12, 12);
            const quint8 hid = quint8(element.at(11));
            if (hid != 0 && !(dropPower && hid == 0x66))
                out += element;
        }
        return out + packet.mid(508);
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

    // What the controller shows at the end of each transition: colors are 8.4 fixed point
    // and each unit adds the slope. Empty when a channel would leave 0..255 and wrap.
    static QList<QColor> simulate(const QColor &start, const QList<Transition> &list)
    {
        int value[3] = {start.red() * 16, start.green() * 16, start.blue() * 16};
        QList<QColor> reached;
        for (const Transition &t : list) {
            const int slope[3] = {t.dr, t.dg, t.db};
            for (int c = 0; c < 3; ++c) {
                value[c] += slope[c] * t.duration;
                if (value[c] < 0 || value[c] > 255 * 16)
                    return {};
            }
            reached.append(QColor(value[0] / 16, value[1] / 16, value[2] / 16));
        }
        return reached;
    }

    static bool near(const QColor &a, const QColor &b, int levels)
    {
        return qAbs(a.red() - b.red()) <= levels && qAbs(a.green() - b.green()) <= levels &&
               qAbs(a.blue() - b.blue()) <= levels;
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
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
        file.write(contents);
    }

    // The real window, offscreen, on an editor whose sends are counted.
    struct Ui {
        // prepare runs before the window opens: seed the library, add themes.
        explicit Ui(const std::function<void(EditorRun &)> &prepare = {})
            : theme(run.dir.path()), translator(langDir.path() + "/language", ":/i18n") {
            if (prepare)
                prepare(run);
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
