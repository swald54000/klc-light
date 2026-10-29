#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "effects.h"
#include "keylabels.h"
#include "layout.h"
#include "profile.h"
#include "protocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "theme.h"
#include "translator.h"

class KlcLightTests : public QObject {
    Q_OBJECT

private slots:
    void primaryActionCounts()
    {
        Backend backend;
        backend.primaryAction();
        QCOMPARE(backend.count(), 1);
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

private:

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
};

QTEST_MAIN(KlcLightTests)
#include "klc-light_tests.moc"
