// Which openSUSE this is, from os-release's ID and VERSION_ID only — never
// NAME or ID_LIKE (docs/design.md, Which systems it runs on). Every mode
// refuses a system this calls unsupported.
#pragma once

#include "filereader.h"

#include <QByteArray>
#include <QString>

namespace gw {

struct SystemIdentity
{
    enum class Family { Rolling, Leap, Unsupported };

    QString id;
    QString versionId;
    QString prettyName;
    Family family = Family::Unsupported;
    QString reason; // plain words, translated; set when unsupported

    bool supported() const { return family != Family::Unsupported; }
};

// Parses os-release text (man os-release: KEY=VALUE lines, values optionally
// in single or double quotes, '#' comments).
[[nodiscard]] SystemIdentity parseOsRelease(const QByteArray &text);

// Reads /etc/os-release, or /usr/lib/os-release only if the first is
// missing; the two are never combined (man os-release).
[[nodiscard]] SystemIdentity readSystemIdentity(const FileReader &files);

// True on a live system, started from a USB stick or disc, which keeps
// nothing after a restart. dracut's live boot puts rd.live.image on the
// kernel command line (measured on Tumbleweed's XFCE live image,
// 2026-10-15).
bool isLiveSystem(const FileReader &files);

} // namespace gw
