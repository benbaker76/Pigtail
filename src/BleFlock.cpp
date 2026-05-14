#include "BleFlock.h"

#include <cctype>
#include <cstring>

// XUNTONG company ID — supplies Flock's BLE radios; in passive scanning this
// is the strongest single signal that an advertisement is Flock infrastructure.
// Source: wgreenberg/flock-you research, referenced from colonelpanichacks/flock-you.
static constexpr uint16_t XUNTONG_COMPANY_ID = 0x09C8;

// Flock-Safety-assigned OUI for fixed cameras / Raven gear.
// Mirrored here so Inspect can flag FlockType from BLE adv payload even when
// the MAC-prefix path (find_or_alloc_track → GetVendor) doesn't fire — BLE
// addresses arrive in NimBLE little-endian and the canonical OUI bytes land
// at val[5..3], not val[0..2].
static constexpr uint8_t FLOCK_OUI[3] = { 0xB4, 0x1E, 0x52 };

// Raven proprietary 16-bit service short UUIDs (top-level services).
// Sourced from GainSec's raven_configurations.json (firmware 1.2.0+).
// These sit above the Bluetooth-SIG-assigned range (>= 0x3000), so matching
// any of them is a strong indicator we're talking to Raven gear.
static const char* const RAVEN_SERVICE_UUIDS[] = {
  "00003100-0000-1000-8000-00805f9b34fb",
  "00003200-0000-1000-8000-00805f9b34fb",
  "00003300-0000-1000-8000-00805f9b34fb",
  "00003400-0000-1000-8000-00805f9b34fb",
  "00003500-0000-1000-8000-00805f9b34fb",
};
static constexpr size_t RAVEN_SERVICE_UUID_COUNT =
    sizeof(RAVEN_SERVICE_UUIDS) / sizeof(RAVEN_SERVICE_UUIDS[0]);

static inline bool memcmpi_f(const uint8_t* a, const uint8_t* b, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (std::tolower(a[i]) != std::tolower(b[i])) return false;
  }
  return true;
}

static inline bool ContainsF(const uint8_t* name, uint8_t name_len, const char* needle) {
  if (!name || !needle) return false;
  const size_t needle_len = strlen(needle);
  if (name_len < needle_len) return false;

  for (size_t i = 0; i <= name_len - needle_len; ++i) {
    if (memcmpi_f(name + i, reinterpret_cast<const uint8_t*>(needle), needle_len)) return true;
  }
  return false;
}

static bool ieq_f(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    char ca = *a++, cb = *b++;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  return *a == 0 && *b == 0;
}

static bool HasRavenService(const NimBLEAdvertisedDevice& dev) {
  for (size_t i = 0; i < RAVEN_SERVICE_UUID_COUNT; ++i) {
    NimBLEUUID uuid(RAVEN_SERVICE_UUIDS[i]);
    if (dev.isAdvertisingService(uuid)) return true;
  }
  return false;
}

FlockInfo BleFlock::Inspect(const NimBLEAdvertisedDevice& dev,
                            const uint8_t* name, uint8_t name_len) const {
  FlockInfo out{};

  // 1) Strongest signal: manufacturer data starting with XUNTONG company id.
  //    Raven service UUIDs (if also advertised) promote the subtype.
  if (dev.haveManufacturerData()) {
    const std::string mfg = dev.getManufacturerData();
    if (mfg.size() >= 2) {
      const auto* b = reinterpret_cast<const uint8_t*>(mfg.data());
      const uint16_t company = (uint16_t)b[0] | (uint16_t(b[1]) << 8);
      if (company == XUNTONG_COMPANY_ID) {
        out.type = HasRavenService(dev) ? FlockType::Raven : FlockType::Camera;
        out.confidence = 90;
        return out;
      }
    }
  }

  // 2) Raven proprietary service UUID advertised without XUNTONG mfg id.
  if (HasRavenService(dev)) {
    out.type = FlockType::Raven;
    out.confidence = 90;
    return out;
  }

  // 3) Device name hints
  if (name && name_len > 0) {
    if (ContainsF(name, name_len, "raven")) {
      out.type = FlockType::Raven;
      out.confidence = 85;
      return out;
    }
    if (ContainsF(name, name_len, "flock")) {
      out.type = FlockType::Camera;
      out.confidence = 85;
      return out;
    }
  }

  // 4) OUI fallback — mirrors the MacPrefixes mapping so FlockType gets set
  //    even when the adv payload carries no other identifying signal.
  const NimBLEAddress addr = dev.getAddress();
  const ble_addr_t* a = addr.getBase();
  if (a && a->val[5] == FLOCK_OUI[0] &&
           a->val[4] == FLOCK_OUI[1] &&
           a->val[3] == FLOCK_OUI[2]) {
    out.type = FlockType::Camera;
    out.confidence = 70;
    return out;
  }

  return out;
}

const char* BleFlock::FlockTypeName(FlockType t) {
  switch (t) {
    case FlockType::Unknown: return "Unknown";
    case FlockType::Camera:  return "Camera";
    case FlockType::Raven:   return "Raven";
    default:                 return "Unknown";
  }
}

bool BleFlock::ParseFlockType(const char* s, FlockType& out) {
  out = FlockType::Unknown;
  if (!s || !*s) return false;

  if (ieq_f(s, "Unknown")) { out = FlockType::Unknown; return true; }
  if (ieq_f(s, "Camera"))  { out = FlockType::Camera;  return true; }
  if (ieq_f(s, "Raven"))   { out = FlockType::Raven;   return true; }

  return false;
}

Vendor BleFlock::GetVendorFromFlockType(FlockType t) {
  switch (t) {
    case FlockType::Camera:
    case FlockType::Raven:
      return Vendor::Flock;
    default:
      return Vendor::Unknown;
  }
}
