#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "keylabels.h"
#include "layout.h"
#include "profile.h"

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

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(KlcLightTests)
#include "klc-light_tests.moc"
