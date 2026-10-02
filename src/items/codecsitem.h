// Media codecs from Packman (GRND-0011, ADR-0003). Commands follow
// openSUSE's SDB:Installing_codecs_from_Packman_repositories, Essentials
// option, read from its web.archive.org snapshot of 2026-05-11 because the
// live page refused automated reads (docs/research/2026-10-02-sources.md).
// GRND-0013 checks them against the live page and on fresh systems.
//
// Browser video needs nothing more: Firefox plays H.264 through the
// system's libavcodec, which this item makes Packman's; OpenH264 covers
// video calls, not playback (SDB:Firefox_MP4/H.264_Video_Support).
#pragma once

#include "core/item.h"

namespace gw {

class CodecsItem : public Item
{
public:
    QString id() const override { return QStringLiteral("media-codecs"); }
    Level level() const override { return Level::Essentials; }
    QString title() const override;
    QString applySentence() const override;
    bool installsPackages() const override { return true; }
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;

    // The alias of an enabled Packman repository, full or Essentials, if
    // one is configured; empty otherwise.
    static QString packmanAlias(const CheckContext &context);
};

} // namespace gw
