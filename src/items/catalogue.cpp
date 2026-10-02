#include "catalogue.h"

#include "broadcomitem.h"
#include "clockitem.h"
#include "codecsitem.h"
#include "firewallitem.h"
#include "firmwareitem.h"
#include "flathubitem.h"
#include "nvidiaitem.h"
#include "poweritem.h"
#include "snapshotsitem.h"
#include "soundfirmwareitem.h"
#include "sshitem.h"
#include "updateitem.h"

namespace gw {

const Catalogue &catalogue()
{
    static const UpdateItem update;
    static const CodecsItem codecs;
    static const FlathubItem flathub;
    static const NvidiaItem nvidia;
    static const SoundFirmwareItem soundFirmware;
    static const BroadcomItem broadcom;
    static const SnapshotsItem snapshots;
    static const FirewallItem firewall;
    static const FirmwareItem firmware;
    static const PowerItem power;
    static const SshItem ssh;
    static const ClockItem clock;
    static const Catalogue all({&update, &codecs, &flathub, &nvidia, &soundFirmware, &broadcom, &snapshots, &firewall, &firmware, &power, &ssh, &clock});
    return all;
}

} // namespace gw
