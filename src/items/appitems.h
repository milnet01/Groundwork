// The Nice-to-have apps (GRND-0025 to GRND-0028, GRND-0030), each its own
// toggle. Two kinds:
//  - a Flathub app: sandboxed and the same on every supported system, with
//    no extra software source; it depends on the Flathub item. Done when
//    `flatpak info APP` succeeds (exit 0 installed, 1 not; measured
//    2026-10-02); installed system-wide with `flatpak install -y
//    --noninteractive --system flathub APP` (flatpak install --help).
//    Also done when the app's usual openSUSE package is installed, so an
//    app installed the ordinary way is not offered twice.
//  - an openSUSE package: done when `rpm -q` finds every package.
// Every app id was confirmed with `flatpak remote-info flathub`, and every
// package name with `zypper info` on Tumbleweed and Leap 16.0 (2026-10-02).
#pragma once

#include "core/item.h"

namespace gw {

class FlathubAppItem : public Item
{
public:
    FlathubAppItem(QString id, const char *title, const char *sentence, QString appId,
                   QStringList nativePackages = {})
        : m_id(std::move(id)), m_title(title), m_sentence(sentence), m_appId(std::move(appId)),
          m_native(std::move(nativePackages)) {}
    QString id() const override { return m_id; }
    Level level() const override { return Level::NiceToHave; }
    QString title() const override;
    QString applySentence() const override;
    QStringList dependsOn() const override { return {QStringLiteral("flathub")}; }
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system, const CheckContext &context) const override;

private:
    QString m_id;
    const char *m_title;
    const char *m_sentence;
    QString m_appId;
    QStringList m_native;
};

class PackageItem : public Item
{
public:
    PackageItem(QString id, const char *title, const char *sentence, QStringList packages)
        : m_id(std::move(id)), m_title(title), m_sentence(sentence), m_packages(std::move(packages)) {}
    QString id() const override { return m_id; }
    Level level() const override { return Level::NiceToHave; }
    QString title() const override;
    QString applySentence() const override;
    bool installsPackages() const override { return true; }
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system, const CheckContext &context) const override;

private:
    QString m_id;
    const char *m_title;
    const char *m_sentence;
    QStringList m_packages;
};

} // namespace gw
