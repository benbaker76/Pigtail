#pragma once
#include <Arduino.h>

enum class EntityKind : uint8_t { WifiClient = 1, BleAdv = 2, WifiAp = 3 };

struct EntityView {
  EntityKind kind;
  uint16_t   index;        // entity index
  uint8_t    addr[6];      // MAC address
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
  bool       has_geo = false;
  double     lat = 0.0;
  double     lon = 0.0;
};

class DeviceTracker {
public:
  bool begin();                 // starts Wi-Fi sniffer + BLE scan + internal tasks
  void setGpsFix(bool valid, double lat, double lon); // optional; safe to call always

  // Build a sorted snapshot into out[]; returns count.
  int buildSnapshot(EntityView* out, int maxOut, float stationary_ratio);

  // Accessors for UI/status
  uint32_t segmentId() const { return _segment_id; }
  uint32_t moveSegments() const { return _move_segments; }
  uint32_t lastEnvTickS() const { return _last_env_tick_s; }

private:
  // internal state is in the .cpp
  uint32_t _segment_id = 1;
  uint32_t _move_segments = 0;
  uint32_t _last_env_tick_s = 0;
};
