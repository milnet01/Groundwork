#include "catalogue.h"

#include "flathubitem.h"

namespace gw {

const Catalogue &catalogue()
{
    static const FlathubItem flathub;
    static const Catalogue all({&flathub});
    return all;
}

} // namespace gw
