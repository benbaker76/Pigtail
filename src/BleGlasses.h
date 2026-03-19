#pragma once

#include <cstdint>
#include <NimBLEDevice.h>

#include "Track.h"

class BleGlasses {
public:
  // Pure classification from an advertisement.
  GlassesInfo Inspect(const NimBLEAdvertisedDevice& dev,
                      const uint8_t* name, uint8_t name_len) const;

  static const char* GlassesTypeName(GlassesType t);
  static bool ParseGlassesType(const char* s, GlassesType& out);
  static Vendor GetVendorFromGlassesType(GlassesType t);

private:
  static constexpr uint16_t META_COMPANY_ID1    = 0x01AB;
  static constexpr uint16_t META_COMPANY_ID2    = 0x058E;
  static constexpr uint16_t ESSILOR_COMPANY_ID  = 0x0D53;
  static constexpr uint16_t SNAP_COMPANY_ID     = 0x03C2;
};
