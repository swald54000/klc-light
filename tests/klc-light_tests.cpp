#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "keylabels.h"
#include "layout.h"
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
