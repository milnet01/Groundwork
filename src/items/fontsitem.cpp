#include "fontsitem.h"

#include <QCoreApplication>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::FontsItem", text); }
} // namespace

QString FontsItem::title() const { return tr("Microsoft-compatible fonts"); }

QString FontsItem::applySentence() const
{
    return tr("Downloads Microsoft's free web fonts, such as Arial and Times New Roman, under "
              "Microsoft's licence, so documents and websites look as intended.");
}

CheckResult FontsItem::check(const CheckContext &context) const
{
    const CommandResult fonts = context.run({QStringLiteral("fc-list"), QStringLiteral(":"), QStringLiteral("family")});
    for (const QByteArray &line : fonts.out.split('\n'))
        if (line == "Arial" || line.startsWith("Arial,"))
            return {CheckState::Done, {}};

    const CommandResult info = context.run({QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("--no-refresh"),
                                            QStringLiteral("info"), QStringLiteral("fetchmsttfonts")});
    if (!info.started || info.timedOut)
        return {CheckState::CouldNotTell, tr("The software sources could not be searched.")};
    if (info.exitCode == 104) // ZYPPER_EXIT_INF_CAP_NOT_FOUND
        return {CheckState::NotNeeded, tr("These fonts are not offered for this system.")};
    if (info.exitCode != 0)
        return {CheckState::CouldNotTell, tr("The software sources could not be searched.")};
    return {CheckState::NotDone, tr("Arial and the other Microsoft fonts are not installed.")};
}

QList<Step> FontsItem::applySteps(const SystemIdentity &, const CheckContext &) const
{
    return {{{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"),
              QStringLiteral("fetchmsttfonts")},
             true, Step::Tool::Zypper, tr("Installing the Microsoft fonts")}};
}

} // namespace gw
