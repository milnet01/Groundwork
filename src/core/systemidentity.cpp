#include "systemidentity.h"

#include <QCoreApplication>
#include <QHash>

namespace gw {
namespace {

QString unquote(QString value)
{
    if (value.size() >= 2 && value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))
        return value.mid(1, value.size() - 2);
    if (value.size() >= 2 && value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"')) {
        value = value.mid(1, value.size() - 2);
        QString out;
        for (qsizetype i = 0; i < value.size(); ++i) {
            if (value[i] == QLatin1Char('\\') && i + 1 < value.size())
                ++i; // \" \\ \$ \` stand for the character itself
            out += value[i];
        }
        return out;
    }
    return value;
}

} // namespace

SystemIdentity parseOsRelease(const QByteArray &text)
{
    QHash<QString, QString> fields;
    for (const QByteArray &rawLine : text.split('\n')) {
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        fields.insert(line.left(eq), unquote(line.mid(eq + 1)));
    }

    SystemIdentity identity;
    identity.id = fields.value(QStringLiteral("ID"));
    identity.versionId = fields.value(QStringLiteral("VERSION_ID"));
    identity.prettyName = fields.value(QStringLiteral("PRETTY_NAME"), identity.id);

    if (identity.id == QLatin1String("opensuse-tumbleweed")
        || identity.id == QLatin1String("opensuse-slowroll")) {
        identity.family = SystemIdentity::Family::Rolling;
    } else if (identity.id == QLatin1String("opensuse-leap")) {
        bool numeric = false;
        const int major = identity.versionId.section(QLatin1Char('.'), 0, 0).toInt(&numeric);
        if (numeric && major >= 16)
            identity.family = SystemIdentity::Family::Leap;
        else
            identity.reason = QCoreApplication::translate("gw::SystemIdentity", "This is openSUSE Leap %1. Groundwork needs Leap 16 or later, "
                                 "or Tumbleweed.").arg(identity.versionId);
    } else if (identity.id.isEmpty()) {
        identity.reason = QCoreApplication::translate("gw::SystemIdentity", "Groundwork could not tell which system this is.");
    } else {
        identity.reason = QCoreApplication::translate("gw::SystemIdentity", "This is %1. Groundwork works on openSUSE Tumbleweed, Slowroll "
                             "and Leap 16 or later.").arg(identity.prettyName);
    }
    return identity;
}

SystemIdentity readSystemIdentity(const FileReader &files)
{
    auto text = files.read(QStringLiteral("/etc/os-release"));
    if (!text && !files.exists(QStringLiteral("/etc/os-release")))
        text = files.read(QStringLiteral("/usr/lib/os-release"));
    if (!text) {
        SystemIdentity identity;
        identity.reason = QCoreApplication::translate("gw::SystemIdentity", "Groundwork could not read this system's release information.");
        return identity;
    }
    return parseOsRelease(*text);
}

} // namespace gw
