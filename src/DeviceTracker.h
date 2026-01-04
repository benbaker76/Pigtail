#pragma once
#include <Arduino.h>
#include <FS.h>
#include "MacPrefixes.h"

enum class EntityKind : uint8_t { WifiClient = 1, BleAdv = 2, WifiAp = 3 };

enum class EntityFlags : uint8_t {
  None           = 0,
  HasGeo         = (1 << 0),
  Watching       = (1 << 1),
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
  bool       tracker_like;

  // AP geo-tagging (valid primarily for WifiAp entries)
  EntityFlags flags = EntityFlags::None;
  double     lat = 0.0;
  double     lon = 0.0;
};

class DeviceTracker {
public:
  bool begin(); // starts Wi-Fi sniffer + BLE scan + internal tasks
  void setGpsFix(bool valid, double lat, double lon); // optional; safe to call always

  // Build a sorted snapshot into out[]; returns count.
  int buildSnapshot(EntityView* out, int maxOut, float stationary_ratio);
  void updateEntity(const EntityView* in);

  // Accessors for UI/status
  uint32_t segmentId() const { return _segment_id; }
  uint32_t moveSegments() const { return _move_segments; }
  uint32_t lastEnvTickS() const { return _last_env_tick_s; }
  void setSdAvailable(bool available) { _sdAvailable = available; }

  void reset();
  void dumpWatchlistFile();
  void outputLists();
  bool readWatchlist();
  bool writeWatchlist();
  bool writeWatchlistKml();

private:
  bool _sdAvailable = false;
  // internal state is in the .cpp
  uint32_t _segment_id = 1;
  uint32_t _move_segments = 0;
  uint32_t _last_env_tick_s = 0;
};
