// Groundwork's entry point. Choosing between the wizard, worker, check and
// askpass modes lands here as each mode is built (docs/design.md, Entry).
#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("groundwork"));
    QCoreApplication::setApplicationVersion(QStringLiteral(GROUNDWORK_VERSION));

    if (QCoreApplication::arguments().contains(QStringLiteral("--version"))) {
        QTextStream(stdout) << "groundwork " << QCoreApplication::applicationVersion() << '\n';
        return 0;
    }
    QTextStream(stderr) << "groundwork: nothing to run yet; try --version\n";
    return 2;
}
