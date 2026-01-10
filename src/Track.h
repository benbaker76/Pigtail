#pragma once

#include "MacPrefixes.h"
#include <cstdint>
#include <type_traits>

static constexpr int FP_TOP_N = 8;

enum class TrackerType : uint8_t {
  Unknown = 0,
  AppleAirPods,
  AppleAirTag,
  AppleFindMy,
  Chipolo,
  GoogleFindHub,
  PebbleBee,
  SmartThingsFind,
  SmartThingsTracker,
  Tile,
};

enum class GoogleFmnManufacturer : uint8_t {
  Unknown = 0,
  PebbleBee,
  Chipolo,
  Eufy,
  Motorola,
  Jio,
  RollingSquare,
};

enum class SamsungTrackerSubtype : uint8_t {
  Unknown = 0,
  SmartTag1,
  SmartTag1Plus,
  SmartTag2,
  Solum,
};

struct TrackerInfo {
  TrackerType           type = TrackerType::Unknown;
  uint8_t               confidence = 0; // 0..100, heuristic score
  GoogleFmnManufacturer google_mfr = GoogleFmnManufacturer::Unknown;
  SamsungTrackerSubtype samsung_subtype = SamsungTrackerSubtype::Unknown;
};

enum class EntityKind : uint8_t { WifiClient = 1, BleAdv = 2, WifiAp = 3 };

enum class EntityFlags : uint8_t {
  None           = 0,
  HasGeo         = (1 << 0),
  Watching       = (1 << 1)
};

constexpr EntityFlags operator|(EntityFlags a, EntityFlags b)
{
  using U = std::underlying_type_t<EntityFlags>;
  return static_cast<EntityFlags>(static_cast<U>(a) | static_cast<U>(b));
}

constexpr EntityFlags operator&(EntityFlags a, EntityFlags b)
{
  using U = std::underlying_type_t<EntityFlags>;
  return static_cast<EntityFlags>(static_cast<U>(a) & static_cast<U>(b));
}

constexpr EntityFlags& operator|=(EntityFlags& a, EntityFlags b)
{
  a = a | b;
  return a;
}

constexpr bool HasFlag(EntityFlags v, EntityFlags f)
{
  using U = std::underlying_type_t<EntityFlags>;
  return (static_cast<U>(v) & static_cast<U>(f)) != 0;
}

constexpr void SetFlag(EntityFlags& v, EntityFlags f)
{
  v |= f;
}

constexpr void ClearFlag(EntityFlags& v, EntityFlags f)
{
  using U = std::underlying_type_t<EntityFlags>;
  v = static_cast<EntityFlags>(static_cast<U>(v) & ~static_cast<U>(f));
}

constexpr void SetFlag(EntityFlags& v, EntityFlags f, bool on)
{
  if (on)
    SetFlag(v, f);
  else
    ClearFlag(v, f);
}

struct EntityView {
  EntityKind kind;
  uint16_t   index;        // entity index
  uint8_t    addr[6];      // MAC address
  Vendor     vendor;       // OUI vendor
  uint8_t    ssid[32];     // SSID
  uint8_t    ssid_len;     // SSID length
  float      score;        // 0..100 (tracks) or 0 for AP anchors
  int        rssi;         // dBm (EMA for tracks, last for AP)
  uint32_t   age_s;        // tracks: last-first; AP: seconds since last seen
  uint32_t   last_seen_s;  // epoch seconds (uptime-based)
  uint32_t   env_hits;
  uint32_t   seen_windows;
  uint32_t   near_windows;
  float      crowd;
  TrackerType tracker_type;
  GoogleFmnManufacturer tracker_google_mfr;
  SamsungTrackerSubtype tracker_samsung_subtype;
  uint8_t tracker_confidence = 0;

  // AP geo-tagging (valid primarily for WifiAp entries)
  EntityFlags flags = EntityFlags::None;
  double     lat = 0.0;
  double     lon = 0.0;
};

enum class TrackKind : uint8_t { WifiClient = 1, BleAdv = 2 };

struct Track {
  bool      in_use = false;
  TrackKind kind{};
  uint8_t   addr[6]{};

  Vendor    vendor = Vendor::Unknown;

  EntityFlags flags = EntityFlags::None;

  uint16_t  index = 0;
  uint32_t  first_seen_s = 0;
  uint32_t  last_seen_s  = 0;

  uint32_t  last_window  = 0;
  uint32_t  seen_windows = 0;
  uint32_t  near_windows = 0;

  float     ema_rssi     = -100.0f;
  float     ema_abs_dev  = 0.0f;

  uint32_t  last_segment_id = 0;
  uint32_t  env_hits        = 0;

  float     crowd_ema = 0.0f;

  // Last-seen GPS fix (where YOU were when you last observed this device)
  uint32_t  last_geo_s = 0;
  double    last_lat = 0.0;
  double    last_lon = 0.0;

  TrackerType tracker_type = TrackerType::Unknown;
  GoogleFmnManufacturer tracker_google_mfr = GoogleFmnManufacturer::Unknown;
  SamsungTrackerSubtype tracker_samsung_subtype = SamsungTrackerSubtype::Unknown;
  uint8_t tracker_confidence = 0;
};

struct Anchor {
  bool     in_use = false;
  uint8_t  addr[6]{};

  Vendor   vendor = Vendor::Unknown;

  EntityFlags flags = EntityFlags::None;

  uint8_t  ssid[32]{};
  uint8_t  ssid_len = 0;

  uint16_t index = 0;
  int      last_rssi = -100;
  uint32_t last_seen_s = 0;

  // Last-seen GPS fix (where YOU were when you last observed this AP)
  uint32_t last_geo_s = 0;
  double   last_lat = 0.0;
  double   last_lon = 0.0;

  // "best pass" (strongest RSSI) location
  int      best_rssi = -127;
  double   best_lat = 0.0;
  double   best_lon = 0.0;

  // Optional running weighted average
  double   w_sum = 0.0;
  double   w_lat = 0.0;
  double   w_lon = 0.0;
};

struct FpItem { uint8_t addr[6]; uint8_t bucket; };
struct EnvFingerprint {
  FpItem items[FP_TOP_N];
  int count = 0;
};
