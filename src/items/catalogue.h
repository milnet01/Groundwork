// Every item, in run order: each listed after what it depends on
// (docs/design.md, The parts). Adding an item means one item file and one
// line in catalogue.cpp.
#pragma once

#include "core/selection.h"

namespace gw {

const Catalogue &catalogue();

} // namespace gw
