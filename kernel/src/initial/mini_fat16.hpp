#pragma once

#include "stdkrn.hpp"

#include <string_view.hpp>
#include <vector.hpp>

namespace kernel::initial::minifat {

// Reads the whole file at `path` (slash-separated, 8.3 components matching
// the layout produced by tools/make-initial-disk.py) off the BIOS boot disk
// into `out`. Returns false if the boot device is missing, the partition/BPB
// cannot be parsed, any path component is not found, or a disk read fails.
// On failure `out` is left cleared. Read-only; no write support.
bool ReadFile(kstd::string_view path, kstd::vector<uint8_t> &out);

}// namespace kernel::initial::minifat
