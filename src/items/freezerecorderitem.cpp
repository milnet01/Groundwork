#include "freezerecorderitem.h"

#include "service.h"

#include <QCoreApplication>
#include <QFile>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::FreezeRecorderItem", text); }

const QString kService = QStringLiteral("freeze-recorder.service");
const QString kBinDir = QStringLiteral("/usr/local/bin");
const QString kScript = kBinDir + QStringLiteral("/freeze-recorder");

// The owner's unit, with what each protection is for. It must keep running,
// and keep writing, at the moment the machine is starving.
const QByteArray kUnit =
    "# Written by Groundwork: the freeze recorder. To undo, run\n"
    "# systemctl disable --now freeze-recorder, then remove this file and\n"
    "# /usr/local/bin/freeze-recorder.\n"
    "[Unit]\n"
    "Description=Freeze recorder - notes memory and I/O pressure before a freeze\n"
    "After=systemd-journald.service\n"
    "\n"
    "[Service]\n"
    "Type=simple\n"
    "ExecStart=/usr/local/bin/freeze-recorder\n"
    "Restart=always\n"
    "RestartSec=5\n"
    "SyslogIdentifier=freeze-recorder\n"
    "# Without these it is the first thing to stall or be killed.\n"
    "OOMScoreAdjust=-900\n"
    "Nice=-10\n"
    "IOSchedulingClass=realtime\n"
    "IOSchedulingPriority=0\n"
    "MemoryMin=32M\n"
    "CPUWeight=200\n"
    "# It reads /proc and the cgroups, and writes only its ring log.\n"
    "ProtectSystem=strict\n"
    "ReadWritePaths=/var/log\n"
    "ProtectHome=yes\n"
    "PrivateTmp=yes\n"
    "NoNewPrivileges=yes\n"
    "RestrictSUIDSGID=yes\n"
    "\n"
    "[Install]\n"
    "WantedBy=multi-user.target\n";

QByteArray script()
{
    QFile f(QStringLiteral(":/items/freeze-recorder.sh"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
} // namespace

QString FreezeRecorderItem::title() const { return tr("The freeze recorder"); }

QString FreezeRecorderItem::applySentence() const
{
    return tr("Keeps a small log of what is using the computer's memory whenever it runs short, so after a "
              "freeze the cause can be found.");
}

CheckResult FreezeRecorderItem::check(const CheckContext &context) const
{
    const ServiceState s = readService(context, kService);
    if (!s.known)
        return {CheckState::CouldNotTell, tr("The service manager did not answer.")};
    if (s.found && s.enabled && s.active)
        return {CheckState::Done, {}};
    return {CheckState::NotDone, tr("Nothing records what the computer was doing before a freeze.")};
}

QList<Step> FreezeRecorderItem::applySteps(const SystemIdentity &, const CheckContext &) const
{
    const QString label = tr("Setting up the freeze recorder");
    const auto systemctl = [&](const QStringList &args) {
        return Step{QStringList{QStringLiteral("systemctl")} + args, true, Step::Tool::Generic, label};
    };
    return {makeDirectoriesStep({kBinDir}, label),
            writeFileStep(kScript, script(), label),
            Step{{QStringLiteral("chmod"), QStringLiteral("755"), kScript}, true, Step::Tool::Generic, label},
            writeFileStep(QStringLiteral("/etc/systemd/system/") + kService, kUnit, label),
            systemctl({QStringLiteral("daemon-reload")}),
            systemctl({QStringLiteral("enable"), kService}),
            // A recorder already running is restarted, so it runs this script.
            systemctl({QStringLiteral("restart"), kService})};
}

} // namespace gw
