// Groundwork's entry point: chooses the mode (docs/design.md, Entry). The
// wizard, worker and askpass modes land here as each is built.
#include "carry/carry.h"
#include "core/checkrunner.h"
#include "core/statepaths.h"
#include "core/systemidentity.h"
#include "core/translations.h"
#include "gui/askpassdialog.h"
#include "gui/themes.h"
#include "gui/wizard.h"
#include "items/catalogue.h"
#include "worker/worker.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QLockFile>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>

#include <csignal>
#include <memory>

#include <unistd.h>

namespace {

// A message before the Wizard opens. Its button is labelled here: Qt has no
// words of its own for some languages offered.
void tell(QMessageBox::Icon icon, const QString &text)
{
    QMessageBox box(icon, QCoreApplication::translate("gw::Entry", "Groundwork"), text, QMessageBox::Ok);
    box.button(QMessageBox::Ok)->setText(QCoreApplication::translate("gw::Entry", "OK"));
    box.exec();
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
    out << QCoreApplication::translate("gw::Entry", "System: %1").arg(identity.prettyName) << '\n';

    const gw::Catalogue &all = gw::catalogue();
    const gw::CheckResults results = gw::runChecks(all.items(), gw::CheckContext(files));
    for (const gw::Item *item : all.items()) {
        const gw::CheckResult r = results.value(item->id());
        out << "  " << item->title() << ": " << gw::stateLine(*item, r) << '\n';
    }

    const QStringList start = all.runOrder(all.defaultSelection(results));
    if (start.isEmpty()) {
        out << QCoreApplication::translate("gw::Entry", "Nothing would start switched on.") << '\n';
    } else {
        QStringList titles;
        for (const QString &id : start)
            titles << all.find(id)->title();
        out << QCoreApplication::translate("gw::Entry", "Would start switched on: %1").arg(titles.join(QStringLiteral(", "))) << '\n';
    }
    return 0;
}

// Wizard mode: refuses root and unsupported systems, then shows the
// Wizard; choosing another language rebuilds it in that language.
int wizardMode(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("groundwork"));
    QApplication::setApplicationVersion(QStringLiteral(GROUNDWORK_VERSION)); // a saved setup records it
    const QFont systemFont = QApplication::font();
    const QStringList args = QApplication::arguments();
    QString language = args.size() == 3 ? args[2] : gw::systemLanguage();
    if (!gw::fontShowsLanguage(language))
        language = QStringLiteral("en"); // rather than a window of empty boxes
    gw::loadLanguage(language);
    gw::applyTheme(QString::fromLatin1(gw::kFollowDesktop));

    if (geteuid() == 0) {
        tell(QMessageBox::Critical,
             QCoreApplication::translate("gw::Entry", "Please start Groundwork as yourself, not as root. "
                                                      "It asks for the password when it needs it."));
        return 1;
    }
    const QString root = qEnvironmentVariable("GROUNDWORK_ROOT", QStringLiteral("/"));
    const gw::FileReader files(root);
    const gw::SystemIdentity identity = gw::readSystemIdentity(files);
    if (!identity.supported()) {
        tell(QMessageBox::Information, identity.reason);
        return 3;
    }

    // One window at a time: two would share the choices and stop files.
    const QString stateDir = gw::defaultStateDir();
    (void)gw::makeStateDir(stateDir);
    QLockFile lock(gw::wizardLockPath(stateDir));
    if (gw::anotherCopyHolds(lock)) {
        tell(QMessageBox::Information,
             QCoreApplication::translate("gw::Entry", "Groundwork is already open. Use the window that is open, "
                                                      "or close it and start Groundwork again."));
        return 1;
    }

    std::unique_ptr<gw::Wizard> wizard;
    std::function<void(const QString &)> build = [&](const QString &code) {
        language = code;
        gw::loadLanguage(language);
        QApplication::setLayoutDirection(gw::directionFor(language));
        // Set only when a language needs its own face; the rest leave the
        // font to Qt and the system.
        if (const QFont font = gw::fontFor(language, systemFont); font != QApplication::font())
            QApplication::setFont(font);
        gw::WizardSetup setup;
        setup.catalogue = &gw::catalogue();
        setup.runChecks = [files] { return gw::runChecks(gw::catalogue().items(), gw::CheckContext(files)); };
        setup.language = language;
        setup.workerProgram = QApplication::applicationFilePath();
        setup.stateDir = stateDir;
        setup.liveSystem = gw::isLiveSystem(files);
        setup.jobs = {gw::Job::SetUp, gw::Job::CarryOver, gw::Job::PrepareDrive};
        setup.surveyDrives = [files] { return gw::surveyDrives(gw::CheckContext(files)); };
        // Settings are the user's own, read from the real home folder.
        setup.gatherSetup = [files] {
            gw::Gathered found;
            gw::gatherPrograms(gw::CheckContext(files), &found);
            gw::gatherSources(gw::CheckContext(files), &found);
            found.settings = gw::gatherSettings(gw::CheckContext(gw::FileReader()), QDir::homePath());
            return found;
        };
        setup.folderSpace = [](const QString &dir) { return gw::readFolderSpace(gw::CheckContext(gw::FileReader()), dir); };
        setup.system = identity;
        // A saved file is the user's, read as the user (GRND-0079 spec 4.1).
        setup.readSetup = [](const QString &file, QString *why, QStringList *skipped) {
            return gw::readSetupFile(gw::CheckContext(gw::FileReader()), file, why, skipped);
        };
        setup.checkItems = [files](const QList<const gw::Item *> &items) {
            return gw::runChecks(items, gw::CheckContext(files));
        };
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
    gw::useFontsOwnBold();

    // Askpass mode: sudo -A runs this program with its prompt as the only
    // argument, so the Worker selects the mode by environment (design, Entry).
    if (qEnvironmentVariable("GROUNDWORK_ASKPASS") == QLatin1String("1")) {
        QApplication app(argc, argv);
        QString language = gw::systemLanguage();
        if (!gw::fontShowsLanguage(language))
            language = QStringLiteral("en");
        gw::loadLanguage(language);
        QApplication::setLayoutDirection(gw::directionFor(language));
        // The Wizard passes its theme down through the Worker (GRND-0050).
        gw::applyTheme(qEnvironmentVariable("GROUNDWORK_THEME", QString::fromLatin1(gw::kFollowDesktop)));
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
        const QString root = qEnvironmentVariable("GROUNDWORK_ROOT", QStringLiteral("/"));
        gw::Worker worker(gw::catalogue(), gw::FileReader(root), gw::defaultStateDir());
        if (const qsizetype at = args.indexOf(QStringLiteral("--prepare-drive")); at > 0 && at + 1 < args.size())
            return worker.prepareDrive(args[at + 1]);
        if (const qsizetype at = args.indexOf(QStringLiteral("--save-setup")); at > 0 && at + 1 < args.size())
            return worker.saveSetup(args[at + 1]);
        if (const qsizetype at = args.indexOf(QStringLiteral("--remove-secrets")); at > 0 && at + 1 < args.size())
            return worker.removeSecrets(args[at + 1]);
        if (const qsizetype at = args.indexOf(QStringLiteral("--delete-setup-file")); at > 0 && at + 1 < args.size())
            return worker.deleteSetupFile(args[at + 1]);
        QString setupChoices;
        QStringList ids;
        QHash<QString, QString> values;
        for (qsizetype i = args.indexOf(QStringLiteral("--worker")) + 1; i < args.size(); ++i) {
            if (args[i] == QLatin1String("--lang")) {
                ++i; // loaded above
                continue;
            }
            if (args[i] == QLatin1String("--setup-choices") && i + 1 < args.size()) {
                setupChoices = args[++i];
                continue;
            }
            if (args[i] == QLatin1String("--set") && i + 1 < args.size()) {
                const QString &pair = args[++i]; // ID=VALUE
                values.insert(pair.section(QLatin1Char('='), 0, 0), pair.section(QLatin1Char('='), 1));
                continue;
            }
            ids << args[i];
        }
        return setupChoices.isEmpty() ? worker.run(ids, values) : worker.runFromSetup(setupChoices, ids, values);
    }
    QTextStream(stderr) << QCoreApplication::translate("gw::Entry", "Usage: groundwork [--lang LANG] --check | --worker [--set ITEM=VALUE] [ITEM...] | --worker --prepare-drive LAYOUT_FILE | --worker --setup-choices CHOICES_FILE [--set ITEM=VALUE] ITEM... | --worker --save-setup CHOICES_FILE | --worker --remove-secrets FILE | --worker --delete-setup-file FILE | --version")
                        << '\n';
    return 2;
}
