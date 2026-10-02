// Groundwork's entry point: chooses the mode (docs/design.md, Entry). The
// wizard, worker and askpass modes land here as each is built.
#include "core/checkrunner.h"
#include "core/systemidentity.h"
#include "gui/askpassdialog.h"
#include "items/catalogue.h"
#include "worker/worker.h"

#include <QApplication>
#include <QCoreApplication>
#include <QTextStream>

#include <csignal>

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
    // Askpass mode: sudo -A runs this program with its prompt as the only
    // argument, so the Worker selects the mode by environment (design, Entry).
    if (qEnvironmentVariable("GROUNDWORK_ASKPASS") == QLatin1String("1")) {
        QApplication app(argc, argv);
        return gw::runAskpass(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString());
    }

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
    if (args.contains(QStringLiteral("--worker"))) {
        // The window may close while a run goes on: writing to a closed
        // output must not end it (design, Stopping).
        std::signal(SIGPIPE, SIG_IGN);
        QStringList ids;
        for (qsizetype i = args.indexOf(QStringLiteral("--worker")) + 1; i < args.size(); ++i) {
            if (args[i] == QLatin1String("--lang")) {
                ++i; // the language is loaded by the translation machinery (GRND-0032)
                continue;
            }
            ids << args[i];
        }
        const QString root = qEnvironmentVariable("GROUNDWORK_ROOT", QStringLiteral("/"));
        gw::Worker worker(gw::catalogue(), gw::FileReader(root), gw::Worker::defaultStateDir());
        return worker.run(ids);
    }
    QTextStream(stderr) << tr("Usage: groundwork --check | --worker [--lang LANG] [ITEM...] | --version")
                        << '\n';
    return 2;
}
