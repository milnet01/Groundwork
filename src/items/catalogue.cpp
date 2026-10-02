#include "catalogue.h"

#include "flathubitem.h"
#include "updateitem.h"

namespace gw {

const Catalogue &catalogue()
{
    static const UpdateItem update;
    static const FlathubItem flathub;
    static const Catalogue all({&update, &flathub});
    return all;
}

} // namespace gw
