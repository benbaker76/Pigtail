#pragma once

#include <cstdint>
#include <NimBLEDevice.h>

#include "Track.h"

class BleFlock {
public:
  // Pure classification from an advertisement.
  FlockInfo Inspect(const NimBLEAdvertisedDevice& dev,
                    const uint8_t* name, uint8_t name_len) const;

  static const char* FlockTypeName(FlockType t);
  static bool ParseFlockType(const char* s, FlockType& out);
  static Vendor GetVendorFromFlockType(FlockType t);

};
