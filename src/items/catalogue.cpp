#include "catalogue.h"

#include "codecsitem.h"
#include "flathubitem.h"
#include "updateitem.h"

namespace gw {

const Catalogue &catalogue()
{
    static const UpdateItem update;
    static const CodecsItem codecs;
    static const FlathubItem flathub;
    static const Catalogue all({&update, &codecs, &flathub});
    return all;
}

} // namespace gw
