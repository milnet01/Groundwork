#include "flathubitem.h"

#include <QCoreApplication>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::FlathubItem", text); }
} // namespace

QString FlathubItem::title() const { return tr("Flatpak and Flathub"); }

QString FlathubItem::applySentence() const
{
    return tr("Installs Flatpak and adds Flathub, the app store most modern Linux apps "
              "ship through.");
}

CheckResult FlathubItem::check(const CheckContext &context) const
{
    const CommandResult r = context.run({QStringLiteral("flatpak"), QStringLiteral("remotes"),
                                         QStringLiteral("--columns=name,options")});
    if (!r.started)
        return {CheckState::NotDone, tr("Flatpak is not installed.")};
    if (!r.ok())
        return {CheckState::CouldNotTell, tr("Flatpak did not list its app sources.")};
    // One remote per line: name, a tab, then its options. A Flathub remote
    // for the whole system or for this user both count.
    for (const QByteArray &line : r.out.split('\n')) {
        if (line.split('\t').value(0).trimmed() == "flathub")
            return {CheckState::Done, {}};
    }
    return {CheckState::NotDone, tr("Flathub is not added yet.")};
}

QList<Step> FlathubItem::applySteps(const SystemIdentity &) const
{
    return {
        {{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"),
          QStringLiteral("flatpak")},
         true, Step::Tool::Zypper, tr("Installing Flatpak")},
        {{QStringLiteral("flatpak"), QStringLiteral("remote-add"), QStringLiteral("--if-not-exists"),
          QStringLiteral("flathub"), QStringLiteral("https://dl.flathub.org/repo/flathub.flatpakrepo")},
         true, Step::Tool::Generic, tr("Adding Flathub")},
    };
}

} // namespace gw
