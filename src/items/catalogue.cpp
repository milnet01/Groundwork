#include "catalogue.h"

#include "clockitem.h"
#include "codecsitem.h"
#include "firewallitem.h"
#include "flathubitem.h"
#include "sshitem.h"
#include "updateitem.h"

namespace gw {

const Catalogue &catalogue()
{
    static const UpdateItem update;
    static const CodecsItem codecs;
    static const FlathubItem flathub;
    static const FirewallItem firewall;
    static const SshItem ssh;
    static const ClockItem clock;
    static const Catalogue all({&update, &codecs, &flathub, &firewall, &ssh, &clock});
    return all;
}

} // namespace gw
