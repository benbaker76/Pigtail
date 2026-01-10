#pragma once

#include <cstdint>
#include <string>

#include <NimBLEDevice.h>   // NimBLEScan, NimBLEAdvertisedDevice

#include "Track.h"          // Track, EntityFlags, Vendor

class BleTracker {
public:
  explicit BleTracker(NimBLEScan* scan);

  // Pure classification from an advertisement.
  TrackerInfo Inspect(const NimBLEAdvertisedDevice& dev) const;

  static Vendor GetVendorFromTrackerType(TrackerType t);
  static const char* TrackerTypeName(TrackerType t);
  static bool ParseTrackerType(const char* s, TrackerType& out);
  static const char* GoogleMfrName(GoogleFmnManufacturer m);
  static bool ParseGoogleMfr(const char* s, GoogleFmnManufacturer& out);
  static const char* SamsungSubtypeName(SamsungTrackerSubtype s);
  static bool ParseSamsungSubtype(const char* s, SamsungTrackerSubtype& out);

private:
  NimBLEScan* _bleScan;

  // Heuristic helpers
  static bool HasService(const NimBLEAdvertisedDevice& dev, const char* uuid_str);
  static bool TryGetAppleMfgPayload(const NimBLEAdvertisedDevice& dev, const uint8_t*& p, size_t& n);

  static GoogleFmnManufacturer GuessGoogleMfrFromName(const std::string& name);
  static SamsungTrackerSubtype GuessSamsungSubtypeFromName(const std::string& name);

  static bool IContains(const std::string& haystack, const char* needle);
};