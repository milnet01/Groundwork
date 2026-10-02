// runChecks keeps every result and never lets "couldn't tell" go without a
// reason.
#include "core/checkrunner.h"

#include <QTemporaryDir>
#include <QtTest>

namespace {

class FakeItem : public gw::Item
{
public:
    FakeItem(QString id, gw::CheckResult result) : m_id(std::move(id)), m_result(std::move(result)) {}
    QString id() const override { return m_id; }
    gw::Level level() const override { return gw::Level::Essentials; }
    QString title() const override { return m_id; }
    QString applySentence() const override { return {}; }
    gw::CheckResult check(const gw::CheckContext &) const override { return m_result; }
    QList<gw::Step> applySteps(const gw::SystemIdentity &, const gw::CheckContext &) const override { return {}; }

private:
    QString m_id;
    gw::CheckResult m_result;
};

} // namespace

class TstCheckRunner : public QObject
{
    Q_OBJECT
private slots:
    void keepsEveryResult()
    {
        QTemporaryDir root;
        const FakeItem done(QStringLiteral("a"), {gw::CheckState::Done, {}});
        const FakeItem notDone(QStringLiteral("b"), {gw::CheckState::NotDone, {}});
        const auto results = gw::runChecks({&done, &notDone}, gw::CheckContext(gw::FileReader(root.path())));
        QCOMPARE(results.size(), 2);
        QCOMPARE(results.value(QStringLiteral("a")).state, gw::CheckState::Done);
        QCOMPARE(results.value(QStringLiteral("b")).state, gw::CheckState::NotDone);
    }

    void couldNotTellAlwaysHasAReason()
    {
        QTemporaryDir root;
        const FakeItem unknown(QStringLiteral("u"), {gw::CheckState::CouldNotTell, {}});
        const auto results = gw::runChecks({&unknown}, gw::CheckContext(gw::FileReader(root.path())));
        QVERIFY(!results.value(QStringLiteral("u")).detail.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstCheckRunner)
#include "tst_checkrunner.moc"
