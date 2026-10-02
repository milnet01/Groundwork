// Groundwork's entry point: chooses the mode (docs/design.md, Entry). The
// wizard, worker and askpass modes land here as each is built.
#include "core/checkrunner.h"
#include "core/statepaths.h"
#include "core/systemidentity.h"
#include "core/translations.h"
#include "gui/askpassdialog.h"
#include "gui/wizard.h"
#include "items/catalogue.h"
#include "worker/worker.h"

#include <QApplication>
#include <QCoreApplication>
#include <QMessageBox>
#include <QTextStream>

#include <csignal>
#include <memory>

#include <unistd.h>

namespace {

QString tr(const char *text) { return QCoreApplication::translate("gw::Entry", text); }

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
        out << "  " << item->title() << ": " << gw::stateText(r.state);
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

// Wizard mode: refuses root and unsupported systems, then shows the
// Wizard; choosing another language rebuilds it in that language.
int wizardMode(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("groundwork"));
    const QStringList args = QApplication::arguments();
    QString language = args.size() == 3 ? args[2] : gw::systemLanguage();
    gw::loadLanguage(language);

    if (geteuid() == 0) {
        QMessageBox::critical(nullptr, tr("Groundwork"),
                              tr("Please start Groundwork as yourself, not as root. "
                                 "It asks for the password when it needs it."));
        return 1;
    }
    const QString root = qEnvironmentVariable("GROUNDWORK_ROOT", QStringLiteral("/"));
    const gw::FileReader files(root);
    const gw::SystemIdentity identity = gw::readSystemIdentity(files);
    if (!identity.supported()) {
        QMessageBox::information(nullptr, tr("Groundwork"), identity.reason);
        return 3;
    }

    std::unique_ptr<gw::Wizard> wizard;
    std::function<void(const QString &)> build = [&](const QString &code) {
        language = code;
        gw::loadLanguage(language);
        QApplication::setLayoutDirection(gw::directionFor(language));
        gw::WizardSetup setup;
        setup.catalogue = &gw::catalogue();
        setup.runChecks = [files] { return gw::runChecks(gw::catalogue().items(), gw::CheckContext(files)); };
        setup.language = language;
        setup.workerProgram = QApplication::applicationFilePath();
        setup.stateDir = gw::defaultStateDir();
        wizard = std::make_unique<gw::Wizard>(setup);
        QObject::connect(wizard.get(), &gw::Wizard::languageChangeRequested, wizard.get(),
                         [&](const QString &next) {
                             QMetaObject::invokeMethod(qApp, [&, next] { build(next); }, Qt::QueuedConnection);
                         });
        wizard->show();
    };
    build(language);
    return app.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    // Askpass mode: sudo -A runs this program with its prompt as the only
    // argument, so the Worker selects the mode by environment (design, Entry).
    if (qEnvironmentVariable("GROUNDWORK_ASKPASS") == QLatin1String("1")) {
        QApplication app(argc, argv);
        gw::loadLanguage(gw::systemLanguage());
        QApplication::setLayoutDirection(gw::directionFor(gw::systemLanguage()));
        return gw::runAskpass(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString());
    }

    // No mode argument: the Wizard (docs/design.md, The shape).
    if (argc == 1 || (argc == 3 && QByteArray(argv[1]) == "--lang"))
        return wizardMode(argc, argv);

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("groundwork"));
    QCoreApplication::setApplicationVersion(QStringLiteral(GROUNDWORK_VERSION));

    const QStringList args = QCoreApplication::arguments();
    // --lang chooses the language; otherwise the system's is used.
    const qsizetype langAt = args.indexOf(QStringLiteral("--lang"));
    const QString language = langAt > 0 && langAt + 1 < args.size() ? args[langAt + 1] : gw::systemLanguage();
    gw::loadLanguage(language);
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
                ++i; // loaded above
                continue;
            }
            ids << args[i];
        }
        const QString root = qEnvironmentVariable("GROUNDWORK_ROOT", QStringLiteral("/"));
        gw::Worker worker(gw::catalogue(), gw::FileReader(root), gw::defaultStateDir());
        return worker.run(ids);
    }
    QTextStream(stderr) << tr("Usage: groundwork [--lang LANG] --check | --worker [ITEM...] | --version")
                        << '\n';
    return 2;
}
