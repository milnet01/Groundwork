#include "codecsitem.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace gw {
namespace {

QString tr(const char *text) { return QCoreApplication::translate("gw::CodecsItem", text); }

const QString kReposDir = QStringLiteral("/etc/zypp/repos.d");
const QString kEssentialsAlias = QStringLiteral("packman-essentials");
const QString kMirror = QStringLiteral("https://ftp.gwdg.de/pub/linux/misc/packman/suse/");

} // namespace

QString CodecsItem::title() const { return tr("Media codecs"); }

QString CodecsItem::applySentence() const
{
    return tr("Adds the Packman Essentials software source, trusts its signing key, and "
              "installs the codecs that let videos and music play, in the browser too.");
}

QString CodecsItem::packmanAlias(const CheckContext &context)
{
    for (const QString &name : context.entries(kReposDir)) {
        if (!name.endsWith(QLatin1String(".repo")))
            continue;
        const auto text = context.readFile(kReposDir + QLatin1Char('/') + name);
        if (!text)
            continue;
        // One .repo file can hold several [alias] sections.
        QString alias;
        bool packman = false;
        bool enabled = true;
        auto finish = [&]() { return packman && enabled ? alias : QString(); };
        for (const QByteArray &raw : text->split('\n')) {
            const QString line = QString::fromUtf8(raw).trimmed();
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                if (!finish().isEmpty())
                    return finish();
                alias = line.mid(1, line.size() - 2);
                packman = false;
                enabled = true;
            } else if (line.startsWith(QLatin1String("baseurl="))) {
                packman = line.contains(QLatin1String("packman"), Qt::CaseInsensitive);
            } else if (line.startsWith(QLatin1String("enabled="))) {
                enabled = line.mid(8).trimmed() == QLatin1String("1");
            }
        }
        if (!finish().isEmpty())
            return finish();
    }
    return {};
}

CheckResult CodecsItem::check(const CheckContext &context) const
{
    const CommandResult r = context.run({QStringLiteral("rpm"), QStringLiteral("-qa"),
                                         QStringLiteral("--qf"), QStringLiteral("%{NAME}\\t%{VENDOR}\\n"),
                                         QStringLiteral("libavcodec*")});
    if (!r.ok())
        return {CheckState::CouldNotTell, tr("The installed packages could not be listed.")};

    // libavcodec<version>, from Packman: the library browsers and players use.
    static const QRegularExpression library(QStringLiteral("^libavcodec\\d+$"));
    bool fromPackman = false;
    for (const QByteArray &line : r.out.split('\n')) {
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() == 2 && library.match(QString::fromUtf8(fields[0])).hasMatch()
            && fields[1].toLower().contains("packman"))
            fromPackman = true;
    }
    const bool repo = !packmanAlias(context).isEmpty();
    if (repo && fromPackman)
        return {CheckState::Done, {}};
    if (repo)
        return {CheckState::NotDone, tr("Packman is added, but the codecs are not installed from it yet.")};
    return {CheckState::NotDone, tr("Videos and music in common formats will not play yet.")};
}

QList<Step> CodecsItem::applySteps(const SystemIdentity &system, const CheckContext &context) const
{
    QList<Step> steps;
    QString alias = packmanAlias(context);
    if (alias.isEmpty()) {
        // A Packman repository already configured, full or Essentials, is
        // used as it is (ADR-0003).
        alias = kEssentialsAlias;
        QString tree;
        if (system.id == QLatin1String("opensuse-slowroll"))
            tree = QStringLiteral("openSUSE_Slowroll");
        else if (system.family == SystemIdentity::Family::Leap)
            tree = QStringLiteral("openSUSE_Leap_$releasever"); // zypper expands it
        else
            tree = QStringLiteral("openSUSE_Tumbleweed");
        steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("addrepo"),
                       QStringLiteral("-cfp"), QStringLiteral("90"),
                       kMirror + tree + QStringLiteral("/Essentials/"), alias},
                      true, Step::Tool::Zypper, tr("Adding the Packman Essentials software source")});
    }
    steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"),
                   QStringLiteral("--gpg-auto-import-keys"), QStringLiteral("refresh"), alias},
                  true, Step::Tool::Zypper, tr("Reading Packman's software list")});
    steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"),
                   QStringLiteral("--allow-vendor-change"), QStringLiteral("--from"), alias,
                   QStringLiteral("ffmpeg"), QStringLiteral("gstreamer-plugins-good"),
                   QStringLiteral("gstreamer-plugins-bad"), QStringLiteral("gstreamer-plugins-ugly"),
                   QStringLiteral("gstreamer-plugins-libav"), QStringLiteral("libavcodec"),
                   QStringLiteral("vlc-codecs")},
                  true, Step::Tool::Zypper, tr("Installing the media codecs")});
    return steps;
}

} // namespace gw
