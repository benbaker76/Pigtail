#include "BleTracker.h"

#include <algorithm>
#include <cctype>

static constexpr uint16_t BT_COMPANY_ID_APPLE = 0x004C;

// BLE “offline finding” service UUIDs (from AirGuard)
static constexpr const char* UUID_FE33 = "0000FE33-0000-1000-8000-00805F9B34FB"; // Apple/Chipolo offline finding
static constexpr const char* UUID_FEAA = "0000FEAA-0000-1000-8000-00805F9B34FB"; // Google Find My Network
static constexpr const char* UUID_FD5A = "0000FD5A-0000-1000-8000-00805F9B34FB"; // Samsung SmartTag
static constexpr const char* UUID_FD69 = "0000FD69-0000-1000-8000-00805F9B34FB"; // Samsung Find My Mobile
static constexpr const char* UUID_FA25 = "0000FA25-0000-1000-8000-00805F9B34FB"; // PebbleBee
static constexpr const char* UUID_FEED = "0000FEED-0000-1000-8000-00805F9B34FB"; // Tile

static inline bool memcmpi(const uint8_t* a, const uint8_t* b, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (std::tolower(a[i]) != std::tolower(b[i])) return false;
  }
  return true;
}

static inline bool Contains(const uint8_t* name, uint8_t name_len, const char* needle) {
  if (!name || !needle) return false;
  const size_t needle_len = strlen(needle);
  if (name_len < needle_len) return false;

  for (size_t i = 0; i <= name_len - needle_len; ++i) {
    if (memcmpi(name + i, reinterpret_cast<const uint8_t*>(needle), needle_len)) return true;
  }
  return false;
}

bool BleTracker::HasService(const NimBLEAdvertisedDevice& dev, const char* uuid_str) {
  // NimBLE-Arduino supports isAdvertisingService() on most builds.
  // If your build differs, replace with dev.haveServiceUUID() + iterate.
  NimBLEUUID uuid(uuid_str);
  return dev.isAdvertisingService(uuid);
}

bool BleTracker::TryGetAppleMfgPayload(const NimBLEAdvertisedDevice& dev, const uint8_t*& p, size_t& n) {
  p = nullptr;
  n = 0;

  if (!dev.haveManufacturerData()) return false;

  const std::string mfg = dev.getManufacturerData();
  if (mfg.size() < 2) return false;

  const auto* b = reinterpret_cast<const uint8_t*>(mfg.data());
  const uint16_t company = (uint16_t)b[0] | (uint16_t(b[1]) << 8);
  if (company != BT_COMPANY_ID_APPLE) return false;

  // Apple payload starts after the 2-byte company id
  p = b + 2;
  n = mfg.size() - 2;
  return true;
}

GoogleFmnManufacturer BleTracker::GuessGoogleMfrFromName(const uint8_t* name, uint8_t name_len) {
  if (name == nullptr || name_len == 0) return GoogleFmnManufacturer::Unknown;

  if (Contains(name, name_len, "pebblebee"))      return GoogleFmnManufacturer::PebbleBee;
  if (Contains(name, name_len, "chipolo"))        return GoogleFmnManufacturer::Chipolo;
  if (Contains(name, name_len, "eufy"))           return GoogleFmnManufacturer::Eufy;
  if (Contains(name, name_len, "motorola"))       return GoogleFmnManufacturer::Motorola;
  if (Contains(name, name_len, "moto"))           return GoogleFmnManufacturer::Motorola;
  if (Contains(name, name_len, "jio"))            return GoogleFmnManufacturer::Jio;
  if (Contains(name, name_len, "rolling square")) return GoogleFmnManufacturer::RollingSquare;
  return GoogleFmnManufacturer::Unknown;
}

  SamsungTrackerSubtype BleTracker::GuessSamsungSubtypeFromName(const uint8_t* name, uint8_t name_len) {
  if (name == nullptr || name_len == 0) return SamsungTrackerSubtype::Unknown;

  // AirGuard does subtype via GATT reads (name/appearance/manufacturer). We keep passive heuristics only:
  if (Contains(name, name_len, "smarttag2") || Contains(name, name_len, "smart tag2") || Contains(name, name_len, "smart tag 2")) {
    return SamsungTrackerSubtype::SmartTag2;
  }
  if (Contains(name, name_len, "solum")) {
    return SamsungTrackerSubtype::Solum;
  }
  if (Contains(name, name_len, "smarttag+") || Contains(name, name_len, "smart tag+")) {
    return SamsungTrackerSubtype::SmartTag1Plus;
  }
  if (Contains(name, name_len, "smarttag") || Contains(name, name_len, "smart tag")) {
    // Could be 1 or 1+, but without UWB bit parsing / GATT we assume SmartTag 1.
    return SamsungTrackerSubtype::SmartTag1;
  }

  return SamsungTrackerSubtype::Unknown;
}

TrackerInfo BleTracker::Inspect(const NimBLEAdvertisedDevice& dev,
                      const uint8_t* name, uint8_t name_len) const {
  TrackerInfo out{};

  // 1) Strong UUID signals first
  if (HasService(dev, UUID_FEED)) {
    out.type = TrackerType::Tile;
    out.confidence = 95;
    return out;
  }

  if (HasService(dev, UUID_FD5A)) {
    out.type = TrackerType::SmartThingsTracker;
    out.confidence = 95;
    out.samsung_subtype = GuessSamsungSubtypeFromName(name, name_len);
    return out;
  }

  if (HasService(dev, UUID_FD69)) {
    out.type = TrackerType::SmartThingsFind;
    out.confidence = 90;
    return out;
  }

  if (HasService(dev, UUID_FEAA)) {
    out.type = TrackerType::GoogleFindHub;
    out.confidence = 90;
    out.google_mfr = GuessGoogleMfrFromName(name, name_len);
    return out;
  }

  if (HasService(dev, UUID_FA25)) {
    out.type = TrackerType::PebbleBee;
    out.confidence = 90;
    return out;
  }

  // 2) Apple mfg data heuristics (from AirGuard filters)
  const uint8_t* ap = nullptr;
  size_t an = 0;
  if (TryGetAppleMfgPayload(dev, ap, an)) {
    // AirGuard: manufacturer payload begins with 0x12, 0x19 for their Apple tracking-related devices.
    // We use those bytes and a conservative third-byte mask similar to their ScanFilter masks.
    if (an >= 2 && ap[0] == 0x12 && ap[1] == 0x19) {
      // If we have a third byte, check the 0x18 mask convention used in AirGuard.
      if (an >= 3) {
        const uint8_t b2 = ap[2];

        // AirPods: (b2 & 0x18) == 0x18 per AirGuard's filter usage.
        if ((b2 & 0x18) == 0x18) {
          out.type = TrackerType::AppleAirPods;
          out.confidence = 85;
          return out;
        }

        // AirTag / FindMy: (b2 & 0x18) == 0x10 per AirGuard usage.
        if ((b2 & 0x18) == 0x10) {
          // If it also advertises FE33, it is very likely a Find My accessory (including some AirTag behaviors),
          // but we use this to differentiate "AppleFindMy" vs "AppleAirTag" without GATT.
          const bool has_fe33 = HasService(dev, UUID_FE33);

          out.type = has_fe33 ? TrackerType::AppleFindMy : TrackerType::AppleAirTag;
          out.confidence = has_fe33 ? 80 : 75;
          return out;
        }
      }

      // Generic Apple “tracking-related” payload but unknown subtype
      out.type = TrackerType::AppleFindMy;
      out.confidence = 65;
      return out;
    }
  }

  // 3) Chipolo vs generic FE33 (non-Apple)
  if (HasService(dev, UUID_FE33)) {
    // If Apple mfg did not match above, treat FE33 as Chipolo/other accessory.
    out.type = TrackerType::Chipolo;
    out.confidence = 80;
    return out;
  }

  // Unknown
  out.type = TrackerType::Unknown;
  out.confidence = 0;
  return out;
}

static bool ieq(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    char ca = *a++, cb = *b++;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  return *a == 0 && *b == 0;
}

void BleTracker::GetName(
    const uint8_t* payload, size_t len,
    uint8_t out[32], uint8_t* out_len)
{
  *out_len = 0;
  if (!payload || len < 2) return;

  size_t i = 0;
  while (i < len) {
    uint8_t ad_len = payload[i];
    if (ad_len == 0) break;
    if (i + 1 + ad_len > len) break;

    uint8_t ad_type = payload[i + 1];
    const uint8_t* ad_data = payload + i + 2;
    size_t ad_data_len = ad_len - 1;

    if (ad_type == BLE_HS_ADV_TYPE_COMP_NAME || ad_type == BLE_HS_ADV_TYPE_INCOMP_NAME) {
      size_t ncopy = std::min<size_t>(ad_data_len, 32);
      if (ncopy) memcpy(out, ad_data, ncopy);
      *out_len = (uint8_t)ncopy;
      return;
    }
    i += (1 + ad_len);
  }
}

Vendor BleTracker::GetVendorFromTrackerType(TrackerType t) {
  switch (t) {
    case TrackerType::AppleAirPods:
    case TrackerType::AppleAirTag:
    case TrackerType::AppleFindMy:
      return Vendor::Apple;
    case TrackerType::Chipolo:
      return Vendor::Chipolo;
    case TrackerType::GoogleFindHub:
      return Vendor::Google;
    case TrackerType::PebbleBee:
      return Vendor::Pebblebee;
    case TrackerType::SmartThingsFind:
    case TrackerType::SmartThingsTracker:
      return Vendor::Samsung;
    case TrackerType::Tile:
      return Vendor::Tile;
    default:
      return Vendor::Unknown;
  }
}

const char* BleTracker::TrackerTypeName(TrackerType t) {
  switch (t) {
    case TrackerType::Unknown:            return "Unknown";
    case TrackerType::AppleAirPods:       return "AirPods";
    case TrackerType::AppleAirTag:        return "AirTag";
    case TrackerType::AppleFindMy:        return "Find My";
    case TrackerType::Chipolo:            return "Chipolo";
    case TrackerType::GoogleFindHub:      return "Find Hub";
    case TrackerType::PebbleBee:          return "PebbleBee";
    case TrackerType::SmartThingsFind:    return "ST Find";
    case TrackerType::SmartThingsTracker: return "ST Tracker";
    case TrackerType::Tile:               return "Tile";
    default:                              return "Unknown";
  }
}

bool BleTracker::ParseTrackerType(const char* s, TrackerType& out) {
  out = TrackerType::Unknown;
  if (!s || !*s) return false;

  if (ieq(s, "Unknown"))   { out = TrackerType::Unknown; return true; }
  if (ieq(s, "AirPods"))   { out = TrackerType::AppleAirPods; return true; }
  if (ieq(s, "AirTag"))    { out = TrackerType::AppleAirTag; return true; }
  if (ieq(s, "Find My"))   { out = TrackerType::AppleFindMy; return true; }
  if (ieq(s, "Chipolo"))   { out = TrackerType::Chipolo; return true; }
  if (ieq(s, "Find Hub"))  { out = TrackerType::GoogleFindHub; return true; }
  if (ieq(s, "PebbleBee")) { out = TrackerType::PebbleBee; return true; }
  if (ieq(s, "Find"))      { out = TrackerType::SmartThingsFind; return true; }
  if (ieq(s, "Tracker"))   { out = TrackerType::SmartThingsTracker; return true; }
  if (ieq(s, "Tile"))      { out = TrackerType::Tile; return true; }

  return false;
}

const char* BleTracker::GoogleMfrName(GoogleFmnManufacturer m) {
  switch (m) {
    case GoogleFmnManufacturer::Unknown:      return "Unknown";
    case GoogleFmnManufacturer::PebbleBee:    return "PebbleBee";
    case GoogleFmnManufacturer::Chipolo:      return "Chipolo";
    case GoogleFmnManufacturer::Eufy:         return "Eufy";
    case GoogleFmnManufacturer::Motorola:     return "Motorola";
    case GoogleFmnManufacturer::Jio:          return "Jio";
    case GoogleFmnManufacturer::RollingSquare:return "Rolling Square";
    default:                                  return "Unknown";
  }
}

bool BleTracker::ParseGoogleMfr(const char* s, GoogleFmnManufacturer& out) {
  out = GoogleFmnManufacturer::Unknown;
  if (!s || !*s) return false;

  if (ieq(s, "Unknown"))       { out = GoogleFmnManufacturer::Unknown; return true; }
  if (ieq(s, "PebbleBee"))     { out = GoogleFmnManufacturer::PebbleBee; return true; }
  if (ieq(s, "Chipolo"))       { out = GoogleFmnManufacturer::Chipolo; return true; }
  if (ieq(s, "Eufy"))          { out = GoogleFmnManufacturer::Eufy; return true; }
  if (ieq(s, "Motorola"))      { out = GoogleFmnManufacturer::Motorola; return true; }
  if (ieq(s, "Jio"))           { out = GoogleFmnManufacturer::Jio; return true; }
  if (ieq(s, "Rolling Square")){ out = GoogleFmnManufacturer::RollingSquare; return true; }

  return false;
}

const char* BleTracker::SamsungSubtypeName(SamsungTrackerSubtype s) {
  switch (s) {
    case SamsungTrackerSubtype::Unknown:      return "Unknown";
    case SamsungTrackerSubtype::SmartTag1:    return "SmartTag 1";
    case SamsungTrackerSubtype::SmartTag1Plus:return "SmartTag+";
    case SamsungTrackerSubtype::SmartTag2:    return "SmartTag 2";
    case SamsungTrackerSubtype::Solum:        return "Solum SmartTag";
    default:                                  return "Unknown";
  }
}

 bool BleTracker::ParseSamsungSubtype(const char* s, SamsungTrackerSubtype& out) {
  out = SamsungTrackerSubtype::Unknown;
  if (!s || !*s) return false;

  if (ieq(s, "Unknown"))       { out = SamsungTrackerSubtype::Unknown; return true; }
  if (ieq(s, "SmartTag 1"))     { out = SamsungTrackerSubtype::SmartTag1; return true; }
  if (ieq(s, "SmartTag+")) { out = SamsungTrackerSubtype::SmartTag1Plus; return true; }
  if (ieq(s, "SmartTag 2"))     { out = SamsungTrackerSubtype::SmartTag2; return true; }
  if (ieq(s, "Solum SmartTag"))         { out = SamsungTrackerSubtype::Solum; return true; }

  return false;
}