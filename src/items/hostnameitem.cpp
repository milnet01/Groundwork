#include "hostnameitem.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::HostnameItem", text); }
} // namespace

QString HostnameItem::title() const { return tr("Computer name"); }

QString HostnameItem::applySentence() const
{
    return tr("Gives this computer the name you type, which other computers on the network see.");
}

QString HostnameItem::valuePrompt() const
{
    return tr("Name (letters, digits and hyphens):");
}

bool HostnameItem::isValidValue(const QString &value) const
{
    static const QRegularExpression label(QStringLiteral("^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$"));
    return label.match(value).hasMatch();
}

CheckResult HostnameItem::check(const CheckContext &context) const
{
    const CommandResult r = context.run({QStringLiteral("hostnamectl"), QStringLiteral("hostname"),
                                         QStringLiteral("--static")});
    if (!r.ok())
        return {CheckState::CouldNotTell, tr("The computer's name could not be read.")};
    const QString name = QString::fromUtf8(r.out).trimmed();
    if (name.isEmpty() || name == QLatin1String("localhost") || name == QLatin1String("localhost.localdomain"))
        return {CheckState::NotDone, tr("This computer has no name of its own yet.")};
    return {CheckState::Done, tr("It is called %1.").arg(name)};
}

QList<Step> HostnameItem::applySteps(const SystemIdentity &, const CheckContext &) const
{
    return {{{QStringLiteral("hostnamectl"), QStringLiteral("hostname"), kValuePlaceholder},
             true, Step::Tool::Generic, tr("Naming the computer")}};
}

} // namespace gw
