#include "swappinessitem.h"

#include <QCoreApplication>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::SwappinessItem", text); }

const QString kDir = QStringLiteral("/etc/sysctl.d");
const QString kFile = kDir + QStringLiteral("/99-groundwork-swappiness.conf");
constexpr int kSwappiness = 10;
} // namespace

QString SwappinessItem::title() const { return tr("Less eager swapping"); }

QString SwappinessItem::applySentence() const
{
    return tr("Keeps programs in memory longer before moving them out to the slower disk.");
}

CheckResult SwappinessItem::check(const CheckContext &context) const
{
    const auto text = context.readFile(QStringLiteral("/proc/sys/vm/swappiness"));
    bool ok = false;
    const int value = text ? QString::fromLatin1(*text).trimmed().toInt(&ok) : 0;
    if (!ok)
        return {CheckState::CouldNotTell, tr("The kernel's setting could not be read.")};
    if (value <= kSwappiness)
        return {CheckState::Done, {}};
    return {CheckState::NotDone, tr("Programs are moved out of memory to the disk early.")};
}

QList<Step> SwappinessItem::applySteps(const SystemIdentity &, const CheckContext &) const
{
    const QString label = tr("Keeping programs in memory longer");
    return {makeDirectoriesStep({kDir}, label),
            writeFileStep(kFile,
                          "# Written by Groundwork: less eager swapping. Remove this file to undo.\n"
                          "vm.swappiness = " + QByteArray::number(kSwappiness) + "\n",
                          label),
            {{QStringLiteral("sysctl"), QStringLiteral("-p"), kFile}, true, Step::Tool::Generic, label}};
}

} // namespace gw
