// Default selection, dependency closure and run order (docs/design.md,
// The levels; S1 and S4).
#include "core/selection.h"

#include <QtTest>

namespace {

class FakeItem : public gw::Item
{
public:
    FakeItem(QString id, gw::Level level, bool installs = false, bool preparation = false,
             QStringList deps = {})
        : m_id(std::move(id)), m_level(level), m_installs(installs), m_prep(preparation),
          m_deps(std::move(deps)) {}
    QString id() const override { return m_id; }
    gw::Level level() const override { return m_level; }
    QString title() const override { return m_id; }
    QString applySentence() const override { return {}; }
    QStringList dependsOn() const override { return m_deps; }
    bool installsPackages() const override { return m_installs; }
    bool isPreparation() const override { return m_prep; }
    gw::CheckResult check(const gw::CheckContext &) const override { return {}; }
    QList<gw::Step> applySteps(const gw::SystemIdentity &, const gw::CheckContext &) const override { return {}; }

private:
    QString m_id;
    gw::Level m_level;
    bool m_installs, m_prep;
    QStringList m_deps;
};

const FakeItem update(QStringLiteral("update"), gw::Level::Essentials, false, true);
const FakeItem codecs(QStringLiteral("codecs"), gw::Level::Essentials, true);
const FakeItem flathub(QStringLiteral("flathub"), gw::Level::Essentials, true);
const FakeItem nvidia(QStringLiteral("nvidia"), gw::Level::Essentials, true);
const FakeItem fonts(QStringLiteral("fonts"), gw::Level::NiceToHave, true);
const FakeItem hostname(QStringLiteral("hostname"), gw::Level::Configuration);

gw::Catalogue catalogue() { return gw::Catalogue({&update, &codecs, &flathub, &nvidia, &hostname, &fonts}); }

gw::CheckResults results(std::initializer_list<std::pair<const char *, gw::CheckState>> states)
{
    gw::CheckResults r;
    for (const auto &[id, state] : states)
        r.insert(QString::fromLatin1(id), {state, {}});
    return r;
}

using S = gw::CheckState;
QStringList sorted(const gw::Selection &s) { QStringList l(s.begin(), s.end()); l.sort(); return l; }

} // namespace

class TstSelection : public QObject
{
    Q_OBJECT
private slots:
    void freshSystemStartsEssentialsAndTheUpdate()
    {
        const auto r = results({{"update", S::NotDone}, {"codecs", S::NotDone}, {"flathub", S::NotDone},
                                {"nvidia", S::NotNeeded}, {"hostname", S::NotDone}, {"fonts", S::NotDone}});
        QCOMPARE(sorted(catalogue().defaultSelection(r)),
                 QStringList({"codecs", "flathub", "update"}));
    }

    void setUpSystemStartsNothingEvenWithUpdatesWaiting() // S1
    {
        const auto r = results({{"update", S::NotDone}, {"codecs", S::Done}, {"flathub", S::Done},
                                {"nvidia", S::NotNeeded}, {"hostname", S::Done}, {"fonts", S::NotDone}});
        QVERIFY(catalogue().defaultSelection(r).isEmpty());
    }

    void switchingOnAnInstallPullsTheUpdateAndSaysSo()
    {
        const auto r = results({{"update", S::NotDone}, {"fonts", S::NotDone}});
        gw::Selection s;
        QCOMPARE(catalogue().switchOn(s, QStringLiteral("fonts"), r), QStringList{"update"});
        QCOMPARE(sorted(s), QStringList({"fonts", "update"}));
    }

    void aDoneDependencyIsNotPulled()
    {
        const auto r = results({{"update", S::Done}, {"fonts", S::NotDone}});
        gw::Selection s;
        QVERIFY(catalogue().switchOn(s, QStringLiteral("fonts"), r).isEmpty());
        QCOMPARE(sorted(s), QStringList{"fonts"});
    }

    void theUpdateCanBeSwitchedOnAlone() // S4
    {
        gw::Selection s;
        catalogue().switchOn(s, QStringLiteral("update"), results({{"update", S::NotDone}}));
        QCOMPARE(sorted(s), QStringList{"update"});
    }

    void switchingOffADependencySwitchesOffWhatNeedsIt()
    {
        gw::Selection s{QStringLiteral("update"), QStringLiteral("codecs"), QStringLiteral("hostname")};
        QCOMPARE(catalogue().switchOff(s, QStringLiteral("update")), QStringList{"codecs"});
        QCOMPARE(sorted(s), QStringList{"hostname"});
    }

    void runOrderIsCatalogueOrder()
    {
        const gw::Selection s{QStringLiteral("fonts"), QStringLiteral("codecs"), QStringLiteral("update")};
        QCOMPARE(catalogue().runOrder(s), QStringList({"update", "codecs", "fonts"}));
    }

    void orderProblemsFindsAnItemBeforeItsDependency()
    {
        QVERIFY(catalogue().orderProblems().isEmpty());
        const gw::Catalogue bad({&codecs, &update});
        QCOMPARE(bad.orderProblems(), QStringList{"codecs is listed before update"});
        const FakeItem orphan(QStringLiteral("orphan"), gw::Level::NiceToHave, false, false,
                              {QStringLiteral("missing")});
        QCOMPARE(gw::Catalogue({&orphan}).orderProblems(), QStringList{"orphan depends on unknown missing"});
    }
};

QTEST_GUILESS_MAIN(TstSelection)
#include "tst_selection.moc"
