// The Nice-to-have app items: Flathub apps against a fake flatpak, package
// items against a fake rpm, and the catalogue's whole shape.
#include "items/appitems.h"
#include "items/catalogue.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstAppItems : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin, m_root;
    QByteArray m_oldPath;

    void fake(const QString &name, const QByteArray &body)
    {
        QFile f(m_bin.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\n" + body + "\n");
        f.close();
        QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
    }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root.path())); }
    static gw::SystemIdentity tumbleweed() { return gw::parseOsRelease("ID=opensuse-tumbleweed\n"); }

private slots:
    void initTestCase()
    {
        m_oldPath = qgetenv("PATH");
        qputenv("PATH", m_bin.path().toUtf8());
    }
    void cleanupTestCase() { qputenv("PATH", m_oldPath); }

    void flathubAppStatesAndSteps()
    {
        const gw::FlathubAppItem app(QStringLiteral("app-x"), "X", "Installs X.", QStringLiteral("org.example.X"));
        fake(QStringLiteral("flatpak"), "[ \"$2\" = org.example.X ] && exit 0; exit 1");
        QCOMPARE(app.check(context()).state, gw::CheckState::Done);
        fake(QStringLiteral("flatpak"), "exit 1");
        QCOMPARE(app.check(context()).state, gw::CheckState::NotDone);
        QCOMPARE(app.dependsOn(), QStringList{"flathub"});
        const gw::FlathubAppItem native(QStringLiteral("app-y"), "Y", "Installs Y.", QStringLiteral("org.example.Y"),
                                        {QStringLiteral("y-native")});
        fake(QStringLiteral("rpm"), "[ \"$2\" = y-native ] && exit 0; exit 1");
        QCOMPARE(native.check(context()).state, gw::CheckState::Done);
        fake(QStringLiteral("rpm"), "exit 1");
        QCOMPARE(native.check(context()).state, gw::CheckState::NotDone);
        const auto steps = app.applySteps(tumbleweed(), context());
        QCOMPARE(steps[0].argv, QStringList({"flatpak", "install", "-y", "--noninteractive", "--system", "flathub", "org.example.X"}));
    }

    void packageItemNeedsEveryPackage()
    {
        const gw::PackageItem item(QStringLiteral("dev-x"), "X", "Installs X.", {QStringLiteral("a"), QStringLiteral("b")});
        fake(QStringLiteral("rpm"), "[ \"$2\" = a ] && exit 0; exit 1"); // a only
        QCOMPARE(item.check(context()).state, gw::CheckState::NotDone);
        fake(QStringLiteral("rpm"), "exit 0");
        QCOMPARE(item.check(context()).state, gw::CheckState::Done);
        QCOMPARE(item.applySteps(tumbleweed(), context())[0].argv, QStringList({"zypper", "-n", "install", "a", "b"}));
    }

    void catalogueShape()
    {
        const auto &all = gw::catalogue();
        QVERIFY(all.orderProblems().isEmpty());
        // Every Flathub app comes after the Flathub item and needs it.
        for (const char *id : {"app-chrome", "app-brave", "app-vlc", "app-discord", "app-zoom", "app-spotify",
                               "game-steam", "game-bottles", "dev-vscodium", "backup-deja-dup"}) {
            const gw::Item *item = all.find(QLatin1String(id));
            QVERIFY2(item, id);
            QVERIFY(item->dependsOn().contains(QStringLiteral("flathub")));
            QCOMPARE(item->level(), gw::Level::NiceToHave);
        }
        // Nice-to-have items never start switched on (S1, S4).
        gw::CheckResults notDone;
        for (const gw::Item *item : all.items())
            notDone.insert(item->id(), {gw::CheckState::NotDone, {}});
        for (const QString &id : all.defaultSelection(notDone))
            QVERIFY2(all.find(id)->level() == gw::Level::Essentials, qPrintable(id));
        // Ids are unique.
        QSet<QString> ids;
        for (const gw::Item *item : all.items())
            QVERIFY2(!ids.contains(item->id()) && (ids.insert(item->id()), true), qPrintable(item->id()));
    }
};

QTEST_GUILESS_MAIN(TstAppItems)
#include "tst_appitems.moc"
