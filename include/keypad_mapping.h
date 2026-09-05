#pragma once

#include <stdint.h>

namespace KeypadMapping {

// Indexed by electrical row * 4 + column. Physical key order was measured as
// scan indices 0,4,8,12,1,5,9,13,2,6,10,14,3,7,11,15.
constexpr uint8_t kMidiNotes[16] = {
    36, 40, 44, 48,
    37, 41, 45, 49,
    38, 42, 46, 50,
    39, 43, 47, 51,
};

}  // namespace KeypadMapping
