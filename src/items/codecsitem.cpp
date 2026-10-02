#include "codecsitem.h"

#include "packman.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace gw {
namespace {

QString tr(const char *text) { return QCoreApplication::translate("gw::CodecsItem", text); }

} // namespace

QString CodecsItem::title() const { return tr("Media codecs"); }

QString CodecsItem::applySentence() const
{
    return tr("Adds the Packman Essentials software source, trusts its signing key, and "
              "installs the codecs that let videos and music play, in the browser too.");
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
    QString alias;
    QList<Step> steps = packmanSteps(system, context, &alias);
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
