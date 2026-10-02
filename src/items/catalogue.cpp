#include "catalogue.h"

#include "broadcomitem.h"
#include "clockitem.h"
#include "codecsitem.h"
#include "firewallitem.h"
#include "firmwareitem.h"
#include "flathubitem.h"
#include "nvidiaitem.h"
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
    static const FirewallItem firewall;
    static const FirmwareItem firmware;
    static const SshItem ssh;
    static const ClockItem clock;
    static const Catalogue all({&update, &codecs, &flathub, &nvidia, &soundFirmware, &broadcom, &firewall, &firmware, &ssh, &clock});
    return all;
}

} // namespace gw
