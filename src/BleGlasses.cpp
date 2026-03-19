#include "BleGlasses.h"

#include <algorithm>
#include <cctype>
#include <cstring>

static inline bool memcmpi_g(const uint8_t* a, const uint8_t* b, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (std::tolower(a[i]) != std::tolower(b[i])) return false;
  }
  return true;
}

static inline bool ContainsG(const uint8_t* name, uint8_t name_len, const char* needle) {
  if (!name || !needle) return false;
  const size_t needle_len = strlen(needle);
  if (name_len < needle_len) return false;

  for (size_t i = 0; i <= name_len - needle_len; ++i) {
    if (memcmpi_g(name + i, reinterpret_cast<const uint8_t*>(needle), needle_len)) return true;
  }
  return false;
}

static bool ieq_g(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    char ca = *a++, cb = *b++;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  return *a == 0 && *b == 0;
}

GlassesInfo BleGlasses::Inspect(const NimBLEAdvertisedDevice& dev,
                                const uint8_t* name, uint8_t name_len) const {
  GlassesInfo out{};

  // 1) Check manufacturer data for known company IDs
  if (dev.haveManufacturerData()) {
    const std::string mfg = dev.getManufacturerData();
    if (mfg.size() >= 2) {
      const auto* b = reinterpret_cast<const uint8_t*>(mfg.data());
      const uint16_t company = (uint16_t)b[0] | (uint16_t(b[1]) << 8);

      if (company == META_COMPANY_ID1 || company == META_COMPANY_ID2) {
        out.type = GlassesType::MetaRayBan;
        out.confidence = 85;
        return out;
      }

      if (company == ESSILOR_COMPANY_ID) {
        out.type = GlassesType::EssilorLuxottica;
        out.confidence = 80;
        return out;
      }

      if (company == SNAP_COMPANY_ID) {
        out.type = GlassesType::SnapSpectacles;
        out.confidence = 85;
        return out;
      }
    }
  }

  // 2) Check device name for Ray-Ban variants
  if (name && name_len > 0) {
    if (ContainsG(name, name_len, "rayban") ||
        ContainsG(name, name_len, "ray-ban") ||
        ContainsG(name, name_len, "ray ban")) {
      out.type = GlassesType::MetaRayBan;
      out.confidence = 90;
      return out;
    }
  }

  return out;
}

const char* BleGlasses::GlassesTypeName(GlassesType t) {
  switch (t) {
    case GlassesType::Unknown:          return "Unknown";
    case GlassesType::MetaRayBan:       return "Meta Ray-Ban";
    case GlassesType::EssilorLuxottica: return "EssilorLuxottica";
    case GlassesType::SnapSpectacles:   return "Snap Spectacles";
    default:                            return "Unknown";
  }
}

bool BleGlasses::ParseGlassesType(const char* s, GlassesType& out) {
  out = GlassesType::Unknown;
  if (!s || !*s) return false;

  if (ieq_g(s, "Unknown"))          { out = GlassesType::Unknown; return true; }
  if (ieq_g(s, "Meta Ray-Ban"))     { out = GlassesType::MetaRayBan; return true; }
  if (ieq_g(s, "EssilorLuxottica")) { out = GlassesType::EssilorLuxottica; return true; }
  if (ieq_g(s, "Snap Spectacles"))  { out = GlassesType::SnapSpectacles; return true; }

  return false;
}

Vendor BleGlasses::GetVendorFromGlassesType(GlassesType t) {
  switch (t) {
    case GlassesType::MetaRayBan:       return Vendor::Meta;
    case GlassesType::EssilorLuxottica: return Vendor::EssilorLuxottica;
    case GlassesType::SnapSpectacles:   return Vendor::Snapchat;
    default:                            return Vendor::Unknown;
  }
}
