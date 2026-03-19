#pragma once

#include <cstdint>

#include <NimBLEDevice.h>   // NimBLEAdvertisedDevice

#include "Track.h"          // Track, EntityFlags, Vendor

class BleTracker {
public:
  BleTracker() = default;

  // Pure classification from an advertisement.
  TrackerInfo Inspect(const NimBLEAdvertisedDevice& dev,
                      const uint8_t* name, uint8_t name_len) const;

  static void GetName(
      const uint8_t* payload, size_t len,
      uint8_t out[32], uint8_t* out_len);
  static Vendor GetVendorFromTrackerType(TrackerType t);
  static const char* TrackerTypeName(TrackerType t);
  static bool ParseTrackerType(const char* s, TrackerType& out);
  static const char* GoogleMfrName(GoogleFmnManufacturer m);
  static bool ParseGoogleMfr(const char* s, GoogleFmnManufacturer& out);
  static const char* SamsungSubtypeName(SamsungTrackerSubtype s);
  static bool ParseSamsungSubtype(const char* s, SamsungTrackerSubtype& out);

private:
  // Heuristic helpers
  static bool HasService(const NimBLEAdvertisedDevice& dev, const char* uuid_str);
  static bool TryGetAppleMfgPayload(const NimBLEAdvertisedDevice& dev, const uint8_t*& p, size_t& n);

  static GoogleFmnManufacturer GuessGoogleMfrFromName(const uint8_t* name, uint8_t name_lene);
  static SamsungTrackerSubtype GuessSamsungSubtypeFromName(const uint8_t* name, uint8_t name_len);
};