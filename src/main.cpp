// Groundwork's entry point: chooses the mode (docs/design.md, Entry). The
// wizard, worker and askpass modes land here as each is built.
#include "core/checkrunner.h"
#include "core/systemidentity.h"
#include "items/catalogue.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

QString tr(const char *text) { return QCoreApplication::translate("gw::Entry", text); }

QString stateLabel(gw::CheckState state)
{
    switch (state) {
    case gw::CheckState::Done: return tr("already done");
    case gw::CheckState::NotDone: return tr("not done");
    case gw::CheckState::NotNeeded: return tr("not needed here");
    case gw::CheckState::CouldNotTell: return tr("couldn't tell");
    }
    return {};
}

// Check mode: print every item's state and what would start switched on.
// Changes nothing. Exits 3 on an unsupported system.
int checkMode(const QString &root)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    const gw::FileReader files(root);
    const gw::SystemIdentity identity = gw::readSystemIdentity(files);
    if (!identity.supported()) {
        err << identity.reason << '\n';
        return 3;
    }
    out << tr("System: %1").arg(identity.prettyName) << '\n';

    const gw::Catalogue &all = gw::catalogue();
    const gw::CheckResults results = gw::runChecks(all.items(), gw::CheckContext(files));
    for (const gw::Item *item : all.items()) {
        const gw::CheckResult r = results.value(item->id());
        out << "  " << item->title() << ": " << stateLabel(r.state);
        if (!r.detail.isEmpty())
            out << " — " << r.detail;
        out << '\n';
    }

    const QStringList start = all.runOrder(all.defaultSelection(results));
    if (start.isEmpty()) {
        out << tr("Nothing would start switched on.") << '\n';
    } else {
        QStringList titles;
        for (const QString &id : start)
            titles << all.find(id)->title();
        out << tr("Would start switched on: %1").arg(titles.join(QStringLiteral(", "))) << '\n';
    }
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("groundwork"));
    QCoreApplication::setApplicationVersion(QStringLiteral(GROUNDWORK_VERSION));

    const QStringList args = QCoreApplication::arguments();
    if (args.contains(QStringLiteral("--version"))) {
        QTextStream(stdout) << "groundwork " << QCoreApplication::applicationVersion() << '\n';
        return 0;
    }
    if (args.contains(QStringLiteral("--check"))) {
        // Tests point this at a fixture tree; in use it is the real root.
        const QString root = qEnvironmentVariable("GROUNDWORK_ROOT", QStringLiteral("/"));
        return checkMode(root);
    }
    QTextStream(stderr) << tr("Usage: groundwork --check | --version") << '\n';
    return 2;
}
