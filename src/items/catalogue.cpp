#include "catalogue.h"

#include "appitems.h"
#include "broadcomitem.h"
#include "clockitem.h"
#include "codecsitem.h"
#include "diskscheduleritem.h"
#include "firewallitem.h"
#include "firmwareitem.h"
#include "flathubitem.h"
#include "fontsitem.h"
#include "hostnameitem.h"
#include "memoryitem.h"
#include "nvidiaitem.h"
#include "poweritem.h"
#include "snapshotsitem.h"
#include "soundfirmwareitem.h"
#include "sshitem.h"
#include "sysrqitem.h"
#include "updateitem.h"
#include "wakeitem.h"

namespace gw {

const Catalogue &catalogue()
{
    // Essentials
    static const UpdateItem update;
    static const CodecsItem codecs;
    static const FlathubItem flathub;
    static const NvidiaItem nvidia;
    static const SoundFirmwareItem soundFirmware;
    static const BroadcomItem broadcom;
    // System setup
    static const SnapshotsItem snapshots;
    static const FirewallItem firewall;
    static const FirmwareItem firmware;
    static const PowerItem power;
    static const MemoryItem memory;
    static const SysrqItem sysrq;
    static const DiskSchedulerItem diskScheduler;
    static const WakeItem calmWake;
    // Configuration
    static const HostnameItem hostname;
    static const SshItem ssh;
    static const ClockItem clock;
    // Nice to have: everyday apps (GRND-0025)
    static const FlathubAppItem chrome(QStringLiteral("app-chrome"), QT_TRANSLATE_NOOP("gw::Apps", "Google Chrome"),
        QT_TRANSLATE_NOOP("gw::Apps", "Installs Google's web browser from Flathub."), QStringLiteral("com.google.Chrome"),
        {QStringLiteral("google-chrome-stable")});
    static const FlathubAppItem brave(QStringLiteral("app-brave"), QT_TRANSLATE_NOOP("gw::Apps", "Brave"),
        QT_TRANSLATE_NOOP("gw::Apps", "Installs the Brave web browser from Flathub."), QStringLiteral("com.brave.Browser"),
        {QStringLiteral("brave-browser")});
    static const FlathubAppItem vlc(QStringLiteral("app-vlc"), QT_TRANSLATE_NOOP("gw::Apps", "VLC media player"),
        QT_TRANSLATE_NOOP("gw::Apps", "Installs VLC, which plays almost any video or music file, from Flathub."),
        QStringLiteral("org.videolan.VLC"),
        {QStringLiteral("vlc")});
    static const FlathubAppItem discord(QStringLiteral("app-discord"), QT_TRANSLATE_NOOP("gw::Apps", "Discord"),
        QT_TRANSLATE_NOOP("gw::Apps", "Installs the Discord chat app from Flathub."), QStringLiteral("com.discordapp.Discord"),
        {QStringLiteral("discord")});
    static const FlathubAppItem zoom(QStringLiteral("app-zoom"), QT_TRANSLATE_NOOP("gw::Apps", "Zoom"),
        QT_TRANSLATE_NOOP("gw::Apps", "Installs the Zoom video-call app from Flathub."), QStringLiteral("us.zoom.Zoom"),
        {QStringLiteral("zoom")});
    static const FlathubAppItem spotify(QStringLiteral("app-spotify"), QT_TRANSLATE_NOOP("gw::Apps", "Spotify"),
        QT_TRANSLATE_NOOP("gw::Apps", "Installs the Spotify music app from Flathub."), QStringLiteral("com.spotify.Client"),
        {QStringLiteral("spotify-client")});
    // Gaming (GRND-0026)
    static const FlathubAppItem steam(QStringLiteral("game-steam"), QT_TRANSLATE_NOOP("gw::Apps", "Steam"),
        QT_TRANSLATE_NOOP("gw::Apps", "Installs Valve's Steam game store from Flathub."), QStringLiteral("com.valvesoftware.Steam"),
        {QStringLiteral("steam")});
    static const FlathubAppItem bottles(QStringLiteral("game-bottles"), QT_TRANSLATE_NOOP("gw::Apps", "Bottles"),
        QT_TRANSLATE_NOOP("gw::Apps", "Installs Bottles, which runs Windows games and programs, from Flathub."),
        QStringLiteral("com.usebottles.bottles"),
        {QStringLiteral("bottles")});
    // Developer tools (GRND-0027)
    static const PackageItem buildTools(QStringLiteral("dev-build-tools"), QT_TRANSLATE_NOOP("gw::Apps", "Build tools"),
        QT_TRANSLATE_NOOP("gw::Apps", "Installs the C and C++ compilers and make, for building software."),
        {QStringLiteral("gcc"), QStringLiteral("gcc-c++"), QStringLiteral("make")});
    static const FlathubAppItem codium(QStringLiteral("dev-vscodium"), QT_TRANSLATE_NOOP("gw::Apps", "VSCodium"),
        QT_TRANSLATE_NOOP("gw::Apps", "Installs VSCodium, a code editor, from Flathub."), QStringLiteral("com.vscodium.codium"),
        {QStringLiteral("codium")});
    // Backup of the user's own files (GRND-0028)
    static const FlathubAppItem dejaDup(QStringLiteral("backup-deja-dup"), QT_TRANSLATE_NOOP("gw::Apps", "Déjà Dup backups"),
        QT_TRANSLATE_NOOP("gw::Apps", "Installs Déjà Dup, which backs up your documents and photos; system snapshots do not."),
        QStringLiteral("org.gnome.DejaDup"),
        {QStringLiteral("deja-dup")});
    // Handy command-line programs (GRND-0030), and fonts (GRND-0029)
    static const PackageItem htop(QStringLiteral("cli-htop"), QT_TRANSLATE_NOOP("gw::Apps", "htop"),
        QT_TRANSLATE_NOOP("gw::Apps", "Installs htop, which shows what is using the computer's memory and processor."),
        {QStringLiteral("htop")});
    static const PackageItem sevenZip(QStringLiteral("cli-7zip"), QT_TRANSLATE_NOOP("gw::Apps", "7-Zip"),
        QT_TRANSLATE_NOOP("gw::Apps", "Installs 7-Zip, for opening .7z and other archives."), {QStringLiteral("7zip")});
    static const PackageItem git(QStringLiteral("cli-git"), QT_TRANSLATE_NOOP("gw::Apps", "Git"),
        QT_TRANSLATE_NOOP("gw::Apps", "Installs Git, for downloading and tracking source code."), {QStringLiteral("git")});
    static const PackageItem fastfetch(QStringLiteral("cli-fastfetch"), QT_TRANSLATE_NOOP("gw::Apps", "fastfetch"),
        QT_TRANSLATE_NOOP("gw::Apps", "Installs fastfetch, which shows a summary of this computer's hardware and system."),
        {QStringLiteral("fastfetch")});
    static const FontsItem fonts;

    static const Catalogue all({&update, &codecs, &flathub, &nvidia, &soundFirmware, &broadcom,
                                &snapshots, &firewall, &firmware, &power, &memory, &sysrq, &diskScheduler, &calmWake,
                                &hostname, &ssh, &clock,
                                &chrome, &brave, &vlc, &discord, &zoom, &spotify, &steam, &bottles,
                                &buildTools, &codium, &dejaDup, &htop, &sevenZip, &git, &fastfetch, &fonts});
    return all;
}

} // namespace gw
