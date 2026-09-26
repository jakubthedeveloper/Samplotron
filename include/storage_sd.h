#pragma once

#include <stdint.h>

namespace StorageSD {

bool init();
// SPI clock the card was mounted at, or 0 when not mounted.
uint32_t spiFrequencyHz();

}  // namespace StorageSD
