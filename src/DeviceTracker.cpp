#include "DeviceTracker.h"
#include "BleTracker.h"
#include "Track.h"

#include <WiFi.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>
#include "esp_wifi.h"

#include <NimBLEDevice.h>
#include "MacPrefixes.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <algorithm>
#include <math.h>
#include <string.h>

// ----------------------------- Tuning -----------------------------

static constexpr int WINDOW_SEC     = 10;
static constexpr int ENV_WINDOW_SEC = 30;

static constexpr int TRACK_IDLE_SEC_WIFI = 15 * 60;
static constexpr int TRACK_IDLE_SEC_BLE  = 20 * 60;
static constexpr int ANCHOR_IDLE_SEC     = 10 * 60;

static constexpr int MAX_TRACKS  = 256;
static constexpr int MAX_ANCHORS = 128;

static constexpr int RSSI_NEAR_DBM = -65;
static constexpr int RSSI_MID_DBM  = -80;

static constexpr float T_CAP_MIN    = 30.0f;
static constexpr float RSSI_DEV_CAP = 10.0f;

static constexpr float CROWD_LO = 5.0f;
static constexpr float CROWD_HI = 40.0f;

static constexpr float FP_SIMILARITY_MIN = 0.50f;

// Wi-Fi hopping
static constexpr uint8_t WIFI_CH_MIN = 1;
static constexpr uint8_t WIFI_CH_MAX = 11;
static constexpr int     HOP_MS      = 250;

static constexpr const char* PATH_WATCHLIST_JSON = "/pt_watchlist.json";
static constexpr const char* PATH_WATCHLIST_KML = "/pt_watchlist.kml";

// ----------------------------- Time helpers -----------------------------

static inline uint64_t now_us() { return (uint64_t)esp_timer_get_time(); }
static inline uint32_t now_s()  { return (uint32_t)(now_us() / 1000000ULL); }
static inline float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

static inline int rssi_bucket(int rssi_dbm) {
  if (rssi_dbm >= RSSI_NEAR_DBM) return 2;
  if (rssi_dbm >= RSSI_MID_DBM)  return 1;
  return 0;
}

static inline void extract_ssid_ie(
  const uint8_t* payload, int len, int ie_start,
  uint8_t out_ssid[32], uint8_t* out_len)
{
  *out_len = 0;
  if (!payload || len <= ie_start) return;

  int i = ie_start;
  while (i + 2 <= len) {
    uint8_t id = payload[i + 0];
    uint8_t l  = payload[i + 1];
    i += 2;

    if (i + l > len) break; // malformed IE list

    if (id == 0) { // SSID
      uint8_t ncopy = (uint8_t)std::min<int>(l, 32);
      if (ncopy) memcpy(out_ssid, payload + i, ncopy);
      *out_len = ncopy; // 0 means hidden
      return;
    }

    i += l;
  }
}

static bool macToString(const uint8_t mac[6], char out[18]) {
  int n = snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return n == 17;
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

// Parses "AA:BB:CC:DD:EE:FF" (also tolerates '-' separators if you want)
static bool parseMac(const char* s, uint8_t out[6]) {
  if (!s) return false;
  for (int i = 0; i < 6; ++i) {
    int hi = hexNibble(s[i * 3 + 0]);
    int lo = hexNibble(s[i * 3 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)((hi << 4) | lo);
    if (i < 5) {
      char sep = s[i * 3 + 2];
      if (sep != ':' && sep != '-') return false;
    }
  }
  return true;
}

static const char* kindToString(EntityKind k) {
  switch (k) {
    case EntityKind::WifiAp:     return "WifiAp";
    case EntityKind::WifiClient: return "WifiClient";
    case EntityKind::BleAdv:     return "BleAdv";
    default:                     return "Unknown";
  }
}

static bool parseKind(const char* s, EntityKind& out) {
  if (!s) return false;
  if (strcmp(s, "WifiAp") == 0)     { out = EntityKind::WifiAp; return true; }
  if (strcmp(s, "WifiClient") == 0) { out = EntityKind::WifiClient; return true; }
  if (strcmp(s, "BleAdv") == 0)     { out = EntityKind::BleAdv; return true; }
  return false;
}

// ----------------------------- Model -----------------------------

static Track  g_tracks[MAX_TRACKS];
static Anchor g_anchors[MAX_ANCHORS];
static uint16_t g_next_index = 1;

// env segmentation
static EnvFingerprint g_last_fp{};
static uint32_t g_last_env_tick_s = 0;
static uint32_t g_segment_id = 1;
static uint32_t g_move_segments = 0;

// crowd estimator
static uint32_t g_current_window = 0;
static uint32_t g_window_unique_hits = 0;

// GPS segmentation (optional)
static bool   g_gps_valid = false;
static double g_gps_lat = 0, g_gps_lon = 0;
static bool   g_gps_anchor_valid = false;
static double g_gps_anchor_lat = 0, g_gps_anchor_lon = 0;
static uint32_t g_last_gps_seg_s = 0;

// concurrency guard
static portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;

// ----------------------------- Observations -----------------------------

enum class ObsKind : uint8_t {
  WifiProbeReq = 1,
  WifiApBeacon = 2,
  WifiApProbeResp = 3,
  BleAdv = 4,
};

struct Observation {
  ObsKind  kind;
  int8_t   rssi_dbm;
  uint8_t  addr[6];
  uint8_t  ssid[32];
  uint8_t  ssid_len;
  uint32_t ts_s;

  TrackerType           tracker_type = TrackerType::Unknown;
  GoogleFmnManufacturer tracker_google_mfr = GoogleFmnManufacturer::Unknown;
  SamsungTrackerSubtype tracker_samsung_subtype = SamsungTrackerSubtype::Unknown;
  uint8_t               tracker_confidence = 0;
};

static QueueHandle_t g_obs_q = nullptr;
static BleTracker* g_bleTracker = nullptr;

// ----------------------------- Helpers -----------------------------

static float score_track(const Track& t, float stationary_ratio) {
  float T_min = (float)(t.last_seen_s - t.first_seen_s) / 60.0f;
  float P = 30.0f * clamp01(log1pf(T_min) / log1pf(T_CAP_MIN));

  float f_near = (t.seen_windows > 0) ? ((float)t.near_windows / (float)t.seen_windows) : 0.0f;
  float stability = clamp01(1.0f - (t.ema_abs_dev / RSSI_DEV_CAP));
  float R = 25.0f * clamp01(0.7f * f_near + 0.3f * stability);

  float move_segments = (float)std::max<uint32_t>(1, g_move_segments);
  float coverage = (float)t.env_hits / move_segments;
  float M = 35.0f * clamp01(coverage);

  float crowd_norm = clamp01((t.crowd_ema - CROWD_LO) / (CROWD_HI - CROWD_LO));
  float C = -25.0f * crowd_norm;

  float I = -20.0f * clamp01(stationary_ratio);

  float S = P + R + M + C + I;
  if (S < 0) S = 0;
  if (S > 100) S = 100;
  return S;
}

static Track* find_or_alloc_track(TrackKind kind, const uint8_t addr[6], uint32_t ts_s) {

  for (int i = 0; i < MAX_TRACKS; i++) {
    if (g_tracks[i].in_use && g_tracks[i].kind == kind && memcmp(g_tracks[i].addr, addr, 6) == 0) return &g_tracks[i];
  }
  for (int i = 0; i < MAX_TRACKS; i++) {
    if (!g_tracks[i].in_use) {
      Track& t = g_tracks[i];
      t = Track{};
      t.in_use = true;
      t.kind = kind;
      memcpy(t.addr, addr, 6);
      t.vendor = GetVendor(addr);
      t.flags = EntityFlags::None;
      t.index = g_next_index++;
      t.first_seen_s = ts_s;
      t.last_seen_s  = ts_s;
      t.last_segment_id = g_segment_id;
      t.env_hits = 1;
      return &t;
    }
  }
  // evict oldest (but never watched)
  int ev = -1;
  uint32_t oldest = UINT32_MAX;

  for (int i = 0; i < MAX_TRACKS; i++) {
    if (!g_tracks[i].in_use) continue;
    if (HasFlag(g_tracks[i].flags, EntityFlags::Watching)) continue;

    if (g_tracks[i].last_seen_s < oldest) { oldest = g_tracks[i].last_seen_s; ev = i; }
  }

  if (ev < 0) return nullptr;

  Track& t = g_tracks[ev];
  t = Track{};
  t.in_use = true;
  t.kind = kind;
  memcpy(t.addr, addr, 6);
  t.vendor = GetVendor(addr);
  t.flags = EntityFlags::None;
  t.index = g_next_index++;
  t.first_seen_s = ts_s;
  t.last_seen_s  = ts_s;
  t.last_segment_id = g_segment_id;
  t.env_hits = 1;
  return &t;
}

static Anchor* find_or_alloc_anchor(const uint8_t bssid[6], uint32_t ts_s) {

  for (int i = 0; i < MAX_ANCHORS; i++) {
    if (g_anchors[i].in_use && memcmp(g_anchors[i].addr, bssid, 6) == 0) return &g_anchors[i];
  }
  for (int i = 0; i < MAX_ANCHORS; i++) {
    if (!g_anchors[i].in_use) {
      Anchor& a = g_anchors[i];
      a = Anchor{};
      a.in_use = true;
      memcpy(a.addr, bssid, 6);
      a.vendor = GetVendor(bssid);
      a.flags = EntityFlags::None;
      a.index = g_next_index++;
      a.last_seen_s = ts_s;
      a.last_rssi = -100;
      return &a;
    }
  }
  // evict oldest (but never watched)
  int ev = -1;
  uint32_t oldest = UINT32_MAX;

  for (int i = 0; i < MAX_ANCHORS; i++) {
    if (!g_anchors[i].in_use) continue;
    if (HasFlag(g_anchors[i].flags, EntityFlags::Watching)) continue;

    if (g_anchors[i].last_seen_s < oldest) { oldest = g_anchors[i].last_seen_s; ev = i; }
  }

  if (ev < 0) return nullptr;

  Anchor& a = g_anchors[ev];
  a = Anchor{};
  a.in_use = true;
  memcpy(a.addr, bssid, 6);
  a.vendor = GetVendor(bssid);
  a.flags = EntityFlags::None;
  a.index = g_next_index++;
  a.last_seen_s = ts_s;
  a.last_rssi = -100;
  return &a;
}

static void update_track_from_obs(Track& t, int rssi_dbm, uint32_t ts_s) {
  t.last_seen_s = ts_s;

  uint32_t window = ts_s / (uint32_t)WINDOW_SEC;
  if (t.last_window != window) {
    t.last_window = window;
    t.seen_windows++;
    if (rssi_dbm >= RSSI_NEAR_DBM) t.near_windows++;

    float alpha = 0.1f;
    t.crowd_ema = (1.0f - alpha) * t.crowd_ema + alpha * (float)g_window_unique_hits;
  }

  float alpha = 0.2f;
  float prev = t.ema_rssi;
  t.ema_rssi = (1.0f - alpha) * t.ema_rssi + alpha * (float)rssi_dbm;

  float dev = fabsf((float)rssi_dbm - prev);
  float beta = 0.2f;
  t.ema_abs_dev = (1.0f - beta) * t.ema_abs_dev + beta * dev;

  if (t.last_segment_id != g_segment_id) {
    t.last_segment_id = g_segment_id;
    t.env_hits++;
  }
}

static EnvFingerprint build_fingerprint(uint32_t ts_s) {
  struct Tmp { uint8_t addr[6]; int rssi; };
  static Tmp tmp[MAX_ANCHORS];
  int n = 0;

  for (int i = 0; i < MAX_ANCHORS; i++) {
    if (!g_anchors[i].in_use) continue;
    if (ts_s - g_anchors[i].last_seen_s > 60) continue;
    Tmp _tmp;
    memcpy(_tmp.addr, g_anchors[i].addr, 6);
    _tmp.rssi = g_anchors[i].last_rssi;
    tmp[n++] = _tmp;
    if (n >= MAX_ANCHORS) break;
  }

  std::sort(tmp, tmp + n, [](const Tmp& a, const Tmp& b) { return a.rssi > b.rssi; });

  EnvFingerprint fp{};
  fp.count = std::min(n, FP_TOP_N);
  for (int i = 0; i < fp.count; i++) {
    memcpy(fp.items[i].addr, tmp[i].addr, 6);
    fp.items[i].bucket = (uint8_t)rssi_bucket(tmp[i].rssi);
  }
  return fp;
}

static float fp_similarity(const EnvFingerprint& a, const EnvFingerprint& b) {
  int uni = 0, inter = 0;
  float bonus = 0.0f;

  auto find_bucket = [](const EnvFingerprint& fp, const uint8_t addr[6], uint8_t& out)->bool {
    for (int i = 0; i < fp.count; i++) if (memcmp(fp.items[i].addr, addr, 6) == 0) { out = fp.items[i].bucket; return true; }
    return false;
  };

  uni += a.count;
  for (int j = 0; j < b.count; j++) {
    bool found = false;
    for (int i = 0; i < a.count; i++) if (memcmp(a.items[i].addr, b.items[j].addr, 6) == 0) { found = true; break; }
    if (!found) uni++;
  }
  if (uni == 0) return 1.0f;

  for (int i = 0; i < a.count; i++) {
    uint8_t bb = 0;
    if (find_bucket(b, a.items[i].addr, bb)) {
      inter++;
      if (bb == a.items[i].bucket) bonus += 0.25f;
    }
  }

  float j = (float)inter / (float)uni;
  return clamp01(j + (bonus / (float)uni));
}

static double deg2rad(double d) { return d * 0.017453292519943295; }
static double haversine_m(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;
  const double dLat = deg2rad(lat2 - lat1);
  const double dLon = deg2rad(lon2 - lon1);
  const double a =
    sin(dLat/2)*sin(dLat/2) +
    cos(deg2rad(lat1))*cos(deg2rad(lat2)) *
    sin(dLon/2)*sin(dLon/2);
  return 2 * R * atan2(sqrt(a), sqrt(1.0 - a));
}

static void maybe_advance_segment(uint32_t ts_s) {
  // Prefer GPS-based segmentation if available and updating
  if (g_gps_valid) {
    if (!g_gps_anchor_valid) {
      g_gps_anchor_valid = true;
      g_gps_anchor_lat = g_gps_lat;
      g_gps_anchor_lon = g_gps_lon;
      g_last_gps_seg_s = ts_s;
      return;
    }

    // Advance segment if moved ~50m from anchor, no more often than every 10s
    if (ts_s - g_last_gps_seg_s >= 10) {
      double d = haversine_m(g_gps_anchor_lat, g_gps_anchor_lon, g_gps_lat, g_gps_lon);
      if (d >= 50.0) {
        g_segment_id++;
        g_move_segments++;
        g_gps_anchor_lat = g_gps_lat;
        g_gps_anchor_lon = g_gps_lon;
        g_last_gps_seg_s = ts_s;
      }
    }
    return;
  }

  // Fallback: AP fingerprint segmentation
  if (g_last_env_tick_s == 0) {
    g_last_env_tick_s = ts_s;
    g_last_fp = build_fingerprint(ts_s);
    return;
  }
  if (ts_s - g_last_env_tick_s < (uint32_t)ENV_WINDOW_SEC) return;
  g_last_env_tick_s = ts_s;

  EnvFingerprint fp = build_fingerprint(ts_s);
  float sim = fp_similarity(fp, g_last_fp);

  if (sim < FP_SIMILARITY_MIN) {
    g_segment_id++;
    g_move_segments++;
  }
  g_last_fp = fp;
}

static void expire_tables(uint32_t ts_s) {
  portENTER_CRITICAL(&g_lock);

  for (int i = 0; i < MAX_TRACKS; i++) {
    if (!g_tracks[i].in_use) continue;

    if (HasFlag(g_tracks[i].flags, EntityFlags::Watching))
      continue; // watched persists

    uint32_t idle = ts_s - g_tracks[i].last_seen_s;
    uint32_t limit = (g_tracks[i].kind == TrackKind::WifiClient) ? TRACK_IDLE_SEC_WIFI : TRACK_IDLE_SEC_BLE;
    if (idle > limit) g_tracks[i].in_use = false;
  }

  for (int i = 0; i < MAX_ANCHORS; i++) {
    if (!g_anchors[i].in_use) continue;

    if (HasFlag(g_anchors[i].flags, EntityFlags::Watching))
      continue; // watched persists

    if (ts_s - g_anchors[i].last_seen_s > (uint32_t)ANCHOR_IDLE_SEC)
      g_anchors[i].in_use = false;
  }

  portEXIT_CRITICAL(&g_lock);
}

static inline float geo_weight_from_rssi(int rssi_dbm) {
  // map RSSI (-95..-35) => weight (1..10)
  return 1.0f + 9.0f * clamp01(((float)rssi_dbm + 95.0f) / 60.0f);
}

static inline void stamp_last_geo(EntityFlags& flags, uint32_t& last_geo_s, double& last_lat, double& last_lon,
                                  uint32_t ts_s, double gps_lat, double gps_lon)
{
  flags |= EntityFlags::HasGeo;
  last_geo_s = ts_s;
  last_lat = gps_lat;
  last_lon = gps_lon;
}

static void process_observation(const Observation& obs) {
  uint32_t window = obs.ts_s / (uint32_t)WINDOW_SEC;
  if (g_current_window != window) {
    g_current_window = window;
    g_window_unique_hits = 0;
  }
  g_window_unique_hits++;

  portENTER_CRITICAL(&g_lock);

  // Copy GPS state atomically under lock
  const bool gps_valid = g_gps_valid;
  const double gps_lat = g_gps_lat;
  const double gps_lon = g_gps_lon;

    switch (obs.kind) {
    case ObsKind::WifiProbeReq: {
      Track* t = find_or_alloc_track(TrackKind::WifiClient, obs.addr, obs.ts_s);
      if (!t) break;
      update_track_from_obs(*t, obs.rssi_dbm, obs.ts_s);

      // NEW: stamp last-seen GPS into the Track
      if (gps_valid) {
        stamp_last_geo(t->flags, t->last_geo_s, t->last_lat, t->last_lon,
                       obs.ts_s, gps_lat, gps_lon);
      }
    } break;

    case ObsKind::BleAdv: {
      Track* t = find_or_alloc_track(TrackKind::BleAdv, obs.addr, obs.ts_s);
      if (!t) break;
      update_track_from_obs(*t, obs.rssi_dbm, obs.ts_s);

      // NEW: stamp last-seen GPS into the Track
      if (gps_valid) {
        stamp_last_geo(t->flags, t->last_geo_s, t->last_lat, t->last_lon,
                       obs.ts_s, gps_lat, gps_lon);
      }

      // NEW: apply tracker results without clobbering known values with Unknown
      if (obs.tracker_type != TrackerType::Unknown) {
        t->tracker_type = obs.tracker_type;

        // Optional vendor inference
        if (t->vendor == Vendor::Unknown) {
          t->vendor = BleTracker::GetVendorFromTrackerType(obs.tracker_type);
        }
      }
      if (obs.tracker_google_mfr != GoogleFmnManufacturer::Unknown)
        t->tracker_google_mfr = obs.tracker_google_mfr;

      if (obs.tracker_samsung_subtype != SamsungTrackerSubtype::Unknown)
        t->tracker_samsung_subtype = obs.tracker_samsung_subtype;

      t->tracker_confidence = max(t->tracker_confidence, obs.tracker_confidence);
    } break;

    case ObsKind::WifiApBeacon:
    case ObsKind::WifiApProbeResp: {
      Anchor* a = find_or_alloc_anchor(obs.addr, obs.ts_s);
      if (!a) break;
      a->last_seen_s = obs.ts_s;
      a->last_rssi   = obs.rssi_dbm;

      if (obs.ssid_len > 0) {
        uint8_t ncopy = (uint8_t)std::min<size_t>(obs.ssid_len, sizeof(a->ssid));
        a->ssid_len = ncopy;
        if (ncopy) memcpy(a->ssid, obs.ssid, ncopy);
      }

      if (gps_valid) {
        const bool hadGeo = HasFlag(a->flags, EntityFlags::HasGeo);

        stamp_last_geo(a->flags, a->last_geo_s, a->last_lat, a->last_lon,
                      obs.ts_s, gps_lat, gps_lon);

        // best pass
        if (!hadGeo || obs.rssi_dbm > a->best_rssi) {
          a->best_rssi = obs.rssi_dbm;
          a->best_lat  = gps_lat;
          a->best_lon  = gps_lon;
        }

        // weighted avg
        float w = geo_weight_from_rssi(obs.rssi_dbm);
        a->w_sum += (double)w;
        a->w_lat += (double)w * gps_lat;
        a->w_lon += (double)w * gps_lon;
      }
    } break;
  }

  portEXIT_CRITICAL(&g_lock);
}


static void wifi_event(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event != ARDUINO_EVENT_WIFI_SCAN_DONE) return;

  // Some cores provide info.wifi_scan_done.number, but not all. Safe: call scanComplete().
  int n = WiFi.scanComplete();
  Serial.printf("[wifi] scan done, n=%d\n", n);

  if (n > 0) {
    for (int i = 0; i < n; i++) {
      Serial.printf("  %2d RSSI=%d CH=%d BSSID=%s SSID=%s%s\n",
        i,
        WiFi.RSSI(i),
        WiFi.channel(i),
        WiFi.BSSIDstr(i).c_str(),
        WiFi.SSID(i).c_str(),
        (WiFi.SSID(i).length() == 0) ? " (hidden)" : ""
      );
      
      Observation obs{};
      obs.kind = ObsKind::WifiApBeacon;
      obs.ts_s = now_s();
      obs.rssi_dbm = (int8_t)WiFi.RSSI(i);
      String ssid = WiFi.SSID(i);
      const uint8_t* bssid = WiFi.BSSID(i);
      memcpy(obs.addr, bssid, 6);
      size_t ncopy = std::min<size_t>(ssid.length(), sizeof(obs.ssid));
      obs.ssid_len = (uint8_t)ncopy;
      if (ncopy) memcpy(obs.ssid, ssid.c_str(), ncopy);
      xQueueSend(g_obs_q, &obs, 0);
    }
  } else if (n == 0) {
    Serial.println("  (no APs found)");
  } else {
    // n < 0 means scan still running or error (-1 running, -2 failed)
    Serial.printf("  scanComplete returned %d\n", n);
  }

  WiFi.scanDelete(); // free memory

  // Kick off next async scan
  WiFi.scanNetworks(true /*async*/, true /*show_hidden*/);
}

// ----------------------------- Wi-Fi promisc parsing -----------------------------

struct __attribute__((packed)) ieee80211_hdr {
  uint16_t fc;
  uint16_t dur;
  uint8_t  addr1[6];
  uint8_t  addr2[6];
  uint8_t  addr3[6];
  uint16_t sc;
};

static inline uint8_t fc_type(uint16_t fc)    { return (fc >> 2) & 0x3; }
static inline uint8_t fc_subtype(uint16_t fc) { return (fc >> 4) & 0xF; }

static void IRAM_ATTR wifi_promisc_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;

  const wifi_promiscuous_pkt_t* ppkt = (wifi_promiscuous_pkt_t*)buf;
  const uint8_t* payload = ppkt->payload;
  const int len = ppkt->rx_ctrl.sig_len;

  if (len < 24) return;

  uint16_t fc = payload[0] | (payload[1] << 8);
  if (fc_type(fc) != 0) return;

  const uint8_t st = fc_subtype(fc);

  Observation obs{};
  obs.ts_s = now_s();
  obs.rssi_dbm = (int8_t)ppkt->rx_ctrl.rssi;

  const ieee80211_hdr* h = (const ieee80211_hdr*)payload;

  if (st == 8 || st == 5) {
    // beacon or probe response:
    // 24-byte header + 12-byte fixed params = 36
    const int ie_start = 36;
    if (len <= ie_start) return;

    obs.kind = (st == 8) ? ObsKind::WifiApBeacon : ObsKind::WifiApProbeResp;
    memcpy(obs.addr, h->addr3, 6); // BSSID

    extract_ssid_ie(payload, len, ie_start, obs.ssid, &obs.ssid_len);

    xQueueSendFromISR(g_obs_q, &obs, nullptr);
  }
  else if (st == 4) {
    // probe request: client SA in addr2; IEs begin immediately after header (24)
    obs.kind = ObsKind::WifiProbeReq;
    memcpy(obs.addr, h->addr2, 6);

    // OPTIONAL: extract probed SSID(s) (often SSID IE is present, may be empty or wildcard)
    const int ie_start = 24;
    if (len > ie_start) {
      extract_ssid_ie(payload, len, ie_start, obs.ssid, &obs.ssid_len);
    }

    xQueueSendFromISR(g_obs_q, &obs, nullptr);
  }
}

// ----------------------------- BLE scanning -----------------------------

static inline void extract_ble_name(
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

    if (ad_type == 0x09 || ad_type == 0x08) { // Complete/Shortened Local Name
      size_t ncopy = std::min<size_t>(ad_data_len, 32);
      if (ncopy) memcpy(out, ad_data, ncopy);
      *out_len = (uint8_t)ncopy;
      return;
    }
    i += (1 + ad_len);
  }
}

class ScanCB : public NimBLEScanCallbacks {
public:
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    Observation obs{};
    obs.kind = ObsKind::BleAdv;
    obs.ts_s = now_s();
    obs.rssi_dbm = (int8_t)dev->getRSSI();

    NimBLEAddress a = dev->getAddress();
    const ble_addr_t* addr_ptr = a.getBase();
    memcpy(obs.addr, addr_ptr->val, 6);

    const std::vector<uint8_t>& p = dev->getPayload();
    extract_ble_name(p.data(), p.size(), obs.ssid, &obs.ssid_len);

    if (g_bleTracker) {
      const TrackerInfo info = g_bleTracker->Inspect(*dev);
      obs.tracker_type = info.type;
      obs.tracker_google_mfr = info.google_mfr;
      obs.tracker_samsung_subtype = info.samsung_subtype;
      obs.tracker_confidence = info.confidence;
    }

    xQueueSend(g_obs_q, &obs, 0);
  }
};

// ----------------------------- Tasks -----------------------------

static void processing_task(void*) {
  Observation obs;
  while (true) {
    if (xQueueReceive(g_obs_q, &obs, pdMS_TO_TICKS(250)) == pdTRUE) {
      process_observation(obs);
    }
    uint32_t ts_s = now_s();
    maybe_advance_segment(ts_s);
    expire_tables(ts_s);
  }
}

static void wifi_hop_task(void*) {
  uint8_t ch = WIFI_CH_MIN;
  while (true) {
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    ch++;
    if (ch > WIFI_CH_MAX) ch = WIFI_CH_MIN;
    vTaskDelay(pdMS_TO_TICKS(HOP_MS));
  }
}

void DeviceTracker::initWifiSniffer() {
  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  //cfg.static_rx_buf_num = 5;
  //cfg.dynamic_rx_buf_num = 0;
  //cfg.dynamic_tx_buf_num = 8;
  esp_wifi_init(&cfg);

  // 2. Start Wi-Fi (Required before scanning or promiscuous mode)
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();

  // 3. Register Event and Start Scan
  // Added here so the system is ready to listen for scan results
  //WiFi.onEvent(wifi_event); 
  //WiFi.scanNetworks(true /*async*/, true /*show_hidden*/);

  // 4. Configure Sniffer Settings
  //esp_wifi_set_ps(WIFI_PS_NONE); // Can't do this with BLE active
  wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&wifi_promisc_cb);

  // 5. Enable Promiscuous Mode
  // If the scan is async, promiscuous mode may interfere with the scan's ability 
  // to hop channels automatically. It is often safer to enable this AFTER 
  // the scan completes in your event handler.
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  Serial.println("Wi-Fi scan started and sniffer initialized");
}

void DeviceTracker::initBleScan() {
  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  _bleScan = NimBLEDevice::getScan();
  _bleScan->setScanCallbacks(new ScanCB(), false);
  _bleScan->setActiveScan(true);
  _bleScan->setInterval(45);
  _bleScan->setWindow(15);
  _bleScan->setMaxResults(0);
  _bleScan->setDuplicateFilter(false);
  _bleScan->start(0, false, false);

  Serial.println("BLE sniffer started");
}

void DeviceTracker::stopBleScan() {
  if (_bleScan) {
    _bleScan->stop();
    NimBLEDevice::deinit(true);
    _bleScan = nullptr;
    Serial.println("BLE sniffer stopped");
  }
}

void DeviceTracker::restartBleScan() {
  stopBleScan();
  initBleScan();
}

void DeviceTracker::initBleTracker()
{
  g_bleTracker = new BleTracker(_bleScan);
}

static constexpr int OBS_Q_LEN = 64;

// Keep Observation as-is for now (we’ll slim it later if needed).
static StaticQueue_t g_obs_q_struct;
static uint8_t g_obs_q_storage[OBS_Q_LEN * sizeof(Observation)];

static void init_obs_queue() {
  g_obs_q = xQueueCreateStatic(
      OBS_Q_LEN,
      sizeof(Observation),
      g_obs_q_storage,
      &g_obs_q_struct
  );
}

static StaticTask_t g_proc_tcb;
static StackType_t  g_proc_stack[8192 / sizeof(StackType_t)];

static StaticTask_t g_hop_tcb;
static StackType_t  g_hop_stack[4096 / sizeof(StackType_t)];

static void start_tasks() {
  xTaskCreateStaticPinnedToCore(processing_task, "dt_proc",
      (uint32_t)(sizeof(g_proc_stack)/sizeof(g_proc_stack[0])),
      nullptr, 10, g_proc_stack, &g_proc_tcb, 0);

  xTaskCreateStaticPinnedToCore(wifi_hop_task, "dt_hop",
      (uint32_t)(sizeof(g_hop_stack)/sizeof(g_hop_stack[0])),
      nullptr, 6, g_hop_stack, &g_hop_tcb, 0);
}


// ----------------------------- DeviceTracker API -----------------------------

bool DeviceTracker::begin() {
  Serial.println("DeviceTracker starting...");

  init_obs_queue();
  if (!g_obs_q) return false;

  initWifiSniffer();
  initBleScan();
  initBleTracker();

  readWatchlist();

  //dumpWatchlistFile();
  //outputLists();

  start_tasks();

  // expose segment stats
  _segment_id = g_segment_id;
  _move_segments = g_move_segments;
  _last_env_tick_s = g_last_env_tick_s;
  return true;
}

void DeviceTracker::setGpsFix(bool valid, double lat, double lon) {
  portENTER_CRITICAL(&g_lock);
  g_gps_valid = valid;
  if (valid) {
    g_gps_lat = lat;
    g_gps_lon = lon;
  } else {
    g_gps_anchor_valid = false;
  }
  portEXIT_CRITICAL(&g_lock);
}

int DeviceTracker::buildSnapshot(EntityView* out, int maxOut, float stationary_ratio) {
  int n = 0;
  uint32_t ts = now_s();

  portENTER_CRITICAL(&g_lock);

  // tracks
  for (int i = 0; i < MAX_TRACKS && n < maxOut; i++) {
    if (!g_tracks[i].in_use) continue;
    const Track& t = g_tracks[i];

    EntityView e{};
    e.kind = (t.kind == TrackKind::WifiClient) ? EntityKind::WifiClient : EntityKind::BleAdv;
    e.index = t.index;
    memcpy(e.addr, t.addr, 6);
    e.vendor = t.vendor;
    e.flags = t.flags;
    e.rssi = (int)lroundf(t.ema_rssi);
    e.age_s = (t.last_seen_s - t.first_seen_s);
    e.last_seen_s = t.last_seen_s;
    e.env_hits = t.env_hits;
    e.seen_windows = t.seen_windows;
    e.near_windows = t.near_windows;
    e.crowd = t.crowd_ema;
    e.score = score_track(t, stationary_ratio);
    e.tracker_type = t.tracker_type;
    e.tracker_google_mfr = t.tracker_google_mfr;
    e.tracker_samsung_subtype = t.tracker_samsung_subtype;
    e.tracker_confidence = t.tracker_confidence;
    if (HasFlag(t.flags, EntityFlags::HasGeo)) {
      e.lat = t.last_lat;
      e.lon = t.last_lon;
    }

    out[n++] = e;
  }

  // anchors (APs): showable, but not “suspicious” by default
  for (int i = 0; i < MAX_ANCHORS && n < maxOut; i++) {
    if (!g_anchors[i].in_use) continue;
    const Anchor& a = g_anchors[i];

    EntityView e{};
    e.kind = EntityKind::WifiAp;
    e.index = a.index;
    memcpy(e.addr, a.addr, 6);
    e.vendor = a.vendor;
    e.flags = a.flags;
    e.ssid_len = std::min<uint8_t>(a.ssid_len, sizeof(e.ssid));
    if (e.ssid_len) memcpy(e.ssid, a.ssid, e.ssid_len);
    e.rssi = a.last_rssi;
    e.age_s = (ts - a.last_seen_s);
    e.last_seen_s = a.last_seen_s;

    e.score = 0.0f;           // anchors not “suspicious” by default
    e.tracker_type = TrackerType::Unknown;
    e.tracker_google_mfr = GoogleFmnManufacturer::Unknown;
    e.tracker_samsung_subtype = SamsungTrackerSubtype::Unknown;
    e.tracker_confidence = 0;

    e.flags = a.flags;
    if (HasFlag(a.flags, EntityFlags::HasGeo)) {
      // Prefer weighted average if it has enough samples, else best-pass
      if (a.w_sum >= 3.0) {
        e.lat = a.w_lat / a.w_sum;
        e.lon = a.w_lon / a.w_sum;
      } else {
        e.lat = a.best_lat;
        e.lon = a.best_lon;
      }
    }

    out[n++] = e;
  }

  portEXIT_CRITICAL(&g_lock);

  std::sort(out, out + n, [](const EntityView& a, const EntityView& b) {
    bool a_watched = HasFlag(a.flags, EntityFlags::Watching);
    bool b_watched = HasFlag(b.flags, EntityFlags::Watching);

    if (a_watched != b_watched) return a_watched > b_watched;
    if (a.score != b.score) return a.score > b.score;
    if (a.rssi != b.rssi) return a.rssi > b.rssi;
    return a.index < b.index;
  });

  _segment_id = g_segment_id;
  _move_segments = g_move_segments;
  _last_env_tick_s = g_last_env_tick_s;

  return n;
}

void DeviceTracker::updateEntity(const EntityView* in)
{
  portENTER_CRITICAL(&g_lock);

  if (in->kind == EntityKind::WifiAp)
  {
    for (int i = 0; i < MAX_ANCHORS; ++i) {
      if (!g_anchors[i].in_use) continue;
      if (g_anchors[i].index != in->index) continue;

      if (HasFlag(in->flags, EntityFlags::Watching))
        SetFlag(g_anchors[i].flags, EntityFlags::Watching);
      else
        ClearFlag(g_anchors[i].flags, EntityFlags::Watching);

      portEXIT_CRITICAL(&g_lock);
      return;
    }
  }
  else
  {
    for (int i = 0; i < MAX_TRACKS; ++i) {
      if (!g_tracks[i].in_use) continue;
      if (g_tracks[i].index != in->index) continue;

      if (HasFlag(in->flags, EntityFlags::Watching))
        SetFlag(g_tracks[i].flags, EntityFlags::Watching);
      else
        ClearFlag(g_tracks[i].flags, EntityFlags::Watching);

      portEXIT_CRITICAL(&g_lock);
      return;
    }
  }

  portEXIT_CRITICAL(&g_lock);
}

void DeviceTracker::reset()
{
  // Clear pending observations so we don't immediately repopulate from old data.
  // NOTE: This may race with ISR producers; if you see issues, consider gating producers
  // with a global "paused" flag checked before xQueueSend*.
  if (g_obs_q) {
    xQueueReset(g_obs_q);
  }

  portENTER_CRITICAL(&g_lock);

  // 1) Clear non-watched tracks/anchors in-place (O(1) extra memory)
  for (int i = 0; i < MAX_TRACKS; ++i) {
    if (!g_tracks[i].in_use) continue;

    if (HasFlag(g_tracks[i].flags, EntityFlags::Watching)) {
      // Leave watched tracks untouched
      continue;
    }

    // Fully clear the slot
    g_tracks[i] = Track{};
  }

  for (int i = 0; i < MAX_ANCHORS; ++i) {
    if (!g_anchors[i].in_use) continue;

    if (HasFlag(g_anchors[i].flags, EntityFlags::Watching)) {
      // Leave watched anchors untouched
      continue;
    }

    g_anchors[i] = Anchor{};
  }

  // 2) Recompute g_next_index so we don't collide with preserved watched entities.
  //    (Preserves stable indices for watched entities.)
  uint16_t maxIdx = 0;
  for (int i = 0; i < MAX_TRACKS; ++i) {
    if (g_tracks[i].in_use) maxIdx = std::max<uint16_t>(maxIdx, g_tracks[i].index);
  }
  for (int i = 0; i < MAX_ANCHORS; ++i) {
    if (g_anchors[i].in_use) maxIdx = std::max<uint16_t>(maxIdx, g_anchors[i].index);
  }
  g_next_index = (uint16_t)(maxIdx + 1);
  if (g_next_index == 0) g_next_index = 1; // paranoia if maxIdx was 0xFFFF

  // 3) Reset env segmentation / movement stats (as before)
  g_last_fp = EnvFingerprint{};
  g_last_env_tick_s = 0;
  g_segment_id = 1;
  g_move_segments = 0;

  // 4) Reset crowd window (as before)
  g_current_window = 0;
  g_window_unique_hits = 0;

  // 5) Reset GPS segmentation anchor (keep current GPS fix validity as-is)
  g_gps_anchor_valid = false;
  g_last_gps_seg_s = 0;

  portEXIT_CRITICAL(&g_lock);

  // Expose reset stats
  _segment_id = g_segment_id;
  _move_segments = g_move_segments;
  _last_env_tick_s = g_last_env_tick_s;
}

bool DeviceTracker::readWatchlist()
{
  fs::FS& fs = SPIFFS;

  File f = fs.open(PATH_WATCHLIST_JSON, FILE_READ);
  if (!f) {
    Serial.printf("[watchlist] no file: %s\n", PATH_WATCHLIST_JSON);
    return false;
  }

  if (f.size() == 0) {
    Serial.printf("[watchlist] file is empty: %s\n", PATH_WATCHLIST_JSON);
    f.close();
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.printf("[watchlist] JSON parse failed: %s\n", err.c_str());
    return false;
  }

  JsonArray items = doc["items"].as<JsonArray>();
  if (items.isNull()) {
    Serial.println("[watchlist] missing 'items'");
    return false;
  }

  const uint32_t ts = now_s();

  uint32_t applied = 0;
  uint32_t skipped = 0;

  portENTER_CRITICAL(&g_lock);

  for (JsonVariant v : items) {
    JsonObject it = v.as<JsonObject>();
    if (it.isNull()) { skipped++; continue; }

    const char* kindStr = it["kind"].as<const char*>();
    const char* macStr  = it["mac"].as<const char*>();

    EntityKind ek;
    uint8_t mac[6]{};
    if (!kindStr || !macStr || !parseKind(kindStr, ek) || !parseMac(macStr, mac)) {
      skipped++;
      continue;
    }

    if (ek == EntityKind::WifiAp) {
      Anchor* a = nullptr;

      for (int i = 0; i < MAX_ANCHORS; ++i) {
        if (g_anchors[i].in_use && memcmp(g_anchors[i].addr, mac, 6) == 0) { a = &g_anchors[i]; break; }
      }

      if (!a) {
        for (int i = 0; i < MAX_ANCHORS; ++i) {
          if (!g_anchors[i].in_use) {
            Anchor& na = g_anchors[i];
            na = Anchor{};
            na.in_use = true;
            memcpy(na.addr, mac, 6);
            na.vendor = GetVendor(mac);
            na.flags  = EntityFlags::None;
            na.index  = g_next_index++;
            na.last_seen_s = ts;
            na.last_rssi   = -95;
            a = &na;
            break;
          }
        }
      }

      if (!a) { skipped++; continue; }

      a->flags |= EntityFlags::Watching;

      if (it["ssid"].is<const char*>()) {
        const char* ss = it["ssid"];
        size_t n = std::min<size_t>(strlen(ss), sizeof(a->ssid));
        a->ssid_len = (uint8_t)n;
        if (n) memcpy(a->ssid, ss, n);
      }

      if (it["lat"].is<double>() && it["lon"].is<double>()) {
        a->best_lat  = (double)it["lat"];
        a->best_lon  = (double)it["lon"];
        a->best_rssi = -127;
        a->w_sum = 0.0; a->w_lat = 0.0; a->w_lon = 0.0;
        a->flags |= EntityFlags::HasGeo;
      }

      applied++;
    }
    else {
      TrackKind tk = (ek == EntityKind::BleAdv) ? TrackKind::BleAdv : TrackKind::WifiClient;
      Track* t = nullptr;

      for (int i = 0; i < MAX_TRACKS; ++i) {
        if (g_tracks[i].in_use && g_tracks[i].kind == tk && memcmp(g_tracks[i].addr, mac, 6) == 0) { t = &g_tracks[i]; break; }
      }

      if (!t) {
        for (int i = 0; i < MAX_TRACKS; ++i) {
          if (!g_tracks[i].in_use) {
            Track& nt = g_tracks[i];
            nt = Track{};
            nt.in_use = true;
            nt.kind   = tk;
            memcpy(nt.addr, mac, 6);
            nt.vendor = GetVendor(mac);
            nt.flags  = EntityFlags::None;
            nt.index  = g_next_index++;
            nt.first_seen_s = ts;
            nt.last_seen_s  = ts;
            nt.ema_rssi     = -95.0f;
            t = &nt;

            if (it["lat"].is<double>() && it["lon"].is<double>()) {
              t->last_lat = (double)it["lat"];
              t->last_lon = (double)it["lon"];
              t->last_geo_s = ts;
              t->flags |= EntityFlags::HasGeo;
            }
            break;
          }
        }
      }

      if (!t) { skipped++; continue; }

      // ---- Restore tracker fields (optional) ----
      // Keep each independent; you can choose to "sanitize" later if desired.
      if (it["tracker_type"].is<const char*>()) {
        TrackerType tt{};
        if (BleTracker::ParseTrackerType(it["tracker_type"].as<const char*>(), tt)) {
          t->tracker_type = tt;
        }
      }

      if (it["tracker_google_mfr"].is<const char*>()) {
        GoogleFmnManufacturer gm{};
        if (BleTracker::ParseGoogleMfr(it["tracker_google_mfr"].as<const char*>(), gm)) {
          t->tracker_google_mfr = gm;
        }
      }

      if (it["tracker_samsung_subtype"].is<const char*>()) {
        SamsungTrackerSubtype ss{};
        if (BleTracker::ParseSamsungSubtype(it["tracker_samsung_subtype"].as<const char*>(), ss)) {
          t->tracker_samsung_subtype = ss;
        }
      }

      if (it["tracker_confidence"].is<uint8_t>()) {
        t->tracker_confidence = it["tracker_confidence"].as<uint8_t>();
      }

      t->flags |= EntityFlags::Watching;
      applied++;
    }
  }

  // Fix next index
  uint16_t maxIdx = 0;
  for (int i = 0; i < MAX_TRACKS; ++i)  if (g_tracks[i].in_use)  maxIdx = std::max<uint16_t>(maxIdx, g_tracks[i].index);
  for (int i = 0; i < MAX_ANCHORS; ++i) if (g_anchors[i].in_use) maxIdx = std::max<uint16_t>(maxIdx, g_anchors[i].index);
  g_next_index = (uint16_t)(maxIdx + 1);
  if (g_next_index == 0) g_next_index = 1;

  portEXIT_CRITICAL(&g_lock);

  Serial.printf("[watchlist] json=%u applied=%u skipped=%u\n",
                (unsigned)items.size(), (unsigned)applied, (unsigned)skipped);

  return applied > 0;
}

void DeviceTracker::dumpWatchlistFile() {
  fs::FS& fs = SPIFFS;
  File f = fs.open(PATH_WATCHLIST_JSON, FILE_READ);
  if (!f) { Serial.println("[watchlist] dump: open failed"); return; }
  Serial.println("[watchlist] dump begin");
  while (f.available()) Serial.write(f.read());
  Serial.println("\n[watchlist] dump end");
  f.close();
}

void DeviceTracker::outputLists()
{
  portENTER_CRITICAL(&g_lock);

  for (int i=0;i<MAX_TRACKS;i++) {
    if (g_tracks[i].in_use && HasFlag(g_tracks[i].flags, EntityFlags::Watching)) {
      char mac[18]; macToString(g_tracks[i].addr, mac);
      Serial.printf("[watch] Track kind=%d idx=%u mac=%s flags=0x%X tt=%s gm=%s ss=%s\n",
        (int)g_tracks[i].kind,
        g_tracks[i].index,
        mac,
        (unsigned)g_tracks[i].flags,
        BleTracker::TrackerTypeName(g_tracks[i].tracker_type),
        BleTracker::GoogleMfrName(g_tracks[i].tracker_google_mfr),
        BleTracker::SamsungSubtypeName(g_tracks[i].tracker_samsung_subtype),
        (unsigned)g_tracks[i].tracker_confidence);
    }
  }

  for (int i=0;i<MAX_ANCHORS;i++) {
    if (g_anchors[i].in_use && HasFlag(g_anchors[i].flags, EntityFlags::Watching)) {
      char mac[18]; macToString(g_anchors[i].addr, mac);
      Serial.printf("[watch] Anchor idx=%u mac=%s ssid_len=%u flags=0x%X\n",
        g_anchors[i].index, mac, g_anchors[i].ssid_len, (unsigned)g_anchors[i].flags);
    }
  }

  portEXIT_CRITICAL(&g_lock);
}

bool DeviceTracker::writeWatchlist()
{
  fs::FS& fs = SPIFFS;

  auto printJsonEscaped = [](Print& p, const uint8_t* s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      unsigned char c = s[i];
      switch (c) {
        case '\\': p.print("\\\\"); break;
        case '\"': p.print("\\\""); break;
        case '\n': p.print("\\n"); break;
        case '\r': p.print("\\r"); break;
        case '\t': p.print("\\t"); break;
        default:
          if (c < 0x20) { /* drop */ }
          else p.print((char)c);
          break;
      }
    }
  };

  File f = fs.open(PATH_WATCHLIST_JSON, FILE_WRITE);
  if (!f) {
    Serial.printf("[watchlist] open failed: %s\n", PATH_WATCHLIST_JSON);
    return false;
  }

  f.print("{\"version\":2,\"items\":[");
  bool first = true;

  // ---------- Anchors ----------
  for (int i = 0; i < MAX_ANCHORS; ++i) {
    bool in_use = false;
    bool watching = false;
    uint8_t mac[6]{};
    uint8_t ssid[32]{};
    uint8_t ssid_len = 0;
    bool hasGeo = false;
    double lat = 0.0, lon = 0.0;

    portENTER_CRITICAL(&g_lock);
    if (g_anchors[i].in_use) {
      in_use = true;
      watching = HasFlag(g_anchors[i].flags, EntityFlags::Watching);
      if (watching) {
        memcpy(mac, g_anchors[i].addr, 6);

        ssid_len = std::min<uint8_t>(g_anchors[i].ssid_len, (uint8_t)sizeof(ssid));
        if (ssid_len) memcpy(ssid, g_anchors[i].ssid, ssid_len);

        hasGeo = HasFlag(g_anchors[i].flags, EntityFlags::HasGeo);
        if (hasGeo) {
          if (g_anchors[i].w_sum >= 3.0) {
            lat = g_anchors[i].w_lat / g_anchors[i].w_sum;
            lon = g_anchors[i].w_lon / g_anchors[i].w_sum;
          } else {
            lat = g_anchors[i].best_lat;
            lon = g_anchors[i].best_lon;
          }
        }
      }
    }
    portEXIT_CRITICAL(&g_lock);

    if (!in_use || !watching) continue;

    char macStr[18];
    if (!macToString(mac, macStr)) continue;

    if (!first) f.print(",");
    first = false;

    f.print("{\"kind\":\"WifiAp\",\"mac\":\"");
    f.print(macStr);
    f.print("\"");

    if (ssid_len > 0) {
      f.print(",\"ssid\":\"");
      printJsonEscaped(f, ssid, ssid_len);
      f.print("\"");
    }

    if (hasGeo) {
      f.print(",\"lat\":"); f.print(lat, 8);
      f.print(",\"lon\":"); f.print(lon, 8);
    }

    f.print("}");
  }

  // ---------- Tracks ----------
  for (int i = 0; i < MAX_TRACKS; ++i) {
    bool in_use = false;
    bool watching = false;
    TrackKind tk{};
    uint8_t mac[6]{};
    bool hasGeo = false;
    double lat = 0.0, lon = 0.0;

    TrackerType tt = TrackerType::Unknown;
    GoogleFmnManufacturer gm = GoogleFmnManufacturer::Unknown;
    SamsungTrackerSubtype ss = SamsungTrackerSubtype::Unknown;
    uint8_t tc = 0;

    portENTER_CRITICAL(&g_lock);
    if (g_tracks[i].in_use) {
      in_use = true;
      watching = HasFlag(g_tracks[i].flags, EntityFlags::Watching);
      if (watching) {
        tk = g_tracks[i].kind;
        memcpy(mac, g_tracks[i].addr, 6);

        hasGeo = HasFlag(g_tracks[i].flags, EntityFlags::HasGeo);
        if (hasGeo) {
          lat = g_tracks[i].last_lat;
          lon = g_tracks[i].last_lon;
        }

        tt = g_tracks[i].tracker_type;
        gm = g_tracks[i].tracker_google_mfr;
        ss = g_tracks[i].tracker_samsung_subtype;
        tc = g_tracks[i].tracker_confidence;
      }
    }
    portEXIT_CRITICAL(&g_lock);

    if (!in_use || !watching) continue;

    char macStr[18];
    if (!macToString(mac, macStr)) continue;

    if (!first) f.print(",");
    first = false;

    const char* kindStr = (tk == TrackKind::BleAdv) ? "BleAdv" : "WifiClient";

    f.print("{\"kind\":\"");
    f.print(kindStr);
    f.print("\",\"mac\":\"");
    f.print(macStr);
    f.print("\"");

    if (hasGeo) {
      f.print(",\"lat\":"); f.print(lat, 8);
      f.print(",\"lon\":"); f.print(lon, 8);
    }

    // ---- NEW: tracker fields ----
    if (tt != TrackerType::Unknown) {
      f.print(",\"tracker_type\":\"");
      f.print(BleTracker::TrackerTypeName(tt));
      f.print("\"");
    }
    if (gm != GoogleFmnManufacturer::Unknown) {
      f.print(",\"tracker_google_mfr\":\"");
      f.print(BleTracker::GoogleMfrName(gm));
      f.print("\"");
    }
    if (ss != SamsungTrackerSubtype::Unknown) {
      f.print(",\"tracker_samsung_subtype\":\"");
      f.print(BleTracker::SamsungSubtypeName(ss));
      f.print("\"");
    }
    if (tc != 0) {
      f.print(",\"tracker_confidence\":");
      f.print((unsigned)tc);
    }

    f.print("}");
  }

  f.print("]}");
  f.close();

  Serial.printf("[watchlist] wrote %s\n", PATH_WATCHLIST_JSON);
  return true;
}

static void printXmlEscaped(Print& p, const char* s) {
  if (!s) return;
  for (; *s; ++s) {
    switch (*s) {
      case '&':  p.print("&amp;");  break;
      case '<':  p.print("&lt;");   break;
      case '>':  p.print("&gt;");   break;
      case '"':  p.print("&quot;"); break;
      case '\'': p.print("&apos;"); break;
      default:   p.print(*s);       break;
    }
  }
}

bool DeviceTracker::writeWatchlistKml()
{
  if (!_sdAvailable) {
    Serial.println("[kml] SD card not available");
    return false;
  }

  fs::FS* fs = static_cast<fs::FS*>(&SD);

  // Overwrite existing file directly
  File f = fs->open(PATH_WATCHLIST_KML, FILE_WRITE);
  if (!f) {
    Serial.printf("[kml] open failed: %s\n", PATH_WATCHLIST_KML);
    SD.end();
    return false;
  }

  // Header
  f.print("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
  f.print("<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n");
  f.print("  <Document>\n");
  f.print("    <name>PT Watchlist</name>\n");

  bool wroteAny = false;

  // ---------------- Anchors (WiFi APs) ----------------
  for (int i = 0; i < MAX_ANCHORS; ++i) {
    bool in_use = false, watching = false, hasGeo = false;
    uint8_t mac[6]{};
    uint8_t ssid[32]{};
    uint8_t ssid_len = 0;
    double lat = 0.0, lon = 0.0;

    // Snapshot under lock
    portENTER_CRITICAL(&g_lock);
    if (g_anchors[i].in_use) {
      in_use   = true;
      watching = HasFlag(g_anchors[i].flags, EntityFlags::Watching);
      hasGeo   = HasFlag(g_anchors[i].flags, EntityFlags::HasGeo);

      if (watching && hasGeo) {
        memcpy(mac, g_anchors[i].addr, 6);

        ssid_len = std::min<uint8_t>(g_anchors[i].ssid_len, (uint8_t)sizeof(ssid));
        if (ssid_len) memcpy(ssid, g_anchors[i].ssid, ssid_len);

        // Prefer your "display" location
        if (g_anchors[i].w_sum >= 3.0) {
          lat = g_anchors[i].w_lat / g_anchors[i].w_sum;
          lon = g_anchors[i].w_lon / g_anchors[i].w_sum;
        } else {
          lat = g_anchors[i].best_lat;
          lon = g_anchors[i].best_lon;
        }
      }
    }
    portEXIT_CRITICAL(&g_lock);

    if (!in_use || !watching || !hasGeo) continue;

    char macStr[18];
    if (!macToString(mac, macStr)) continue;

    char ssidStr[33]{0};
    if (ssid_len > 0) {
      size_t n = std::min<size_t>(ssid_len, 32);
      memcpy(ssidStr, ssid, n);
      ssidStr[n] = 0;
    }

    f.print("    <Placemark>\n");
    f.print("      <name>");
    if (ssid_len > 0) {
      printXmlEscaped(f, ssidStr);
      f.print(" (");
      f.print(macStr);
      f.print(")");
    } else {
      f.print(macStr);
    }
    f.print("</name>\n");

    f.print("      <description>");
    f.print("Kind: WifiAp&#10;MAC: ");
    f.print(macStr);
    if (ssid_len > 0) {
      f.print("&#10;SSID: ");
      printXmlEscaped(f, ssidStr);
    }
    f.print("</description>\n");

    f.print("      <Point>\n");
    f.print("        <coordinates>");
    // KML coordinates are lon,lat,alt
    f.print(lon, 8);
    f.print(",");
    f.print(lat, 8);
    f.print(",0</coordinates>\n");
    f.print("      </Point>\n");
    f.print("    </Placemark>\n");

    wroteAny = true;
  }

  // ---------------- Tracks (WiFi clients / BLE) ----------------
  for (int i = 0; i < MAX_TRACKS; ++i) {
    bool in_use = false, watching = false, hasGeo = false;
    TrackKind tk{};
    uint8_t mac[6]{};
    double lat = 0.0, lon = 0.0;

    TrackerType tt = TrackerType::Unknown;
    GoogleFmnManufacturer gm = GoogleFmnManufacturer::Unknown;
    SamsungTrackerSubtype ss = SamsungTrackerSubtype::Unknown;
    uint8_t tc = 0;

    portENTER_CRITICAL(&g_lock);
    if (g_tracks[i].in_use) {
      in_use   = true;
      watching = HasFlag(g_tracks[i].flags, EntityFlags::Watching);
      hasGeo   = HasFlag(g_tracks[i].flags, EntityFlags::HasGeo);

      if (watching && hasGeo) {
        tk = g_tracks[i].kind;
        memcpy(mac, g_tracks[i].addr, 6);
        lat = g_tracks[i].last_lat;
        lon = g_tracks[i].last_lon;

        tt = g_tracks[i].tracker_type;
        gm = g_tracks[i].tracker_google_mfr;
        ss = g_tracks[i].tracker_samsung_subtype;
        tc = g_tracks[i].tracker_confidence;
      }
    }
    portEXIT_CRITICAL(&g_lock);

    if (!in_use || !watching || !hasGeo) continue;

    char macStr[18];
    if (!macToString(mac, macStr)) continue;

    const char* kindStr = (tk == TrackKind::BleAdv) ? "BleAdv" : "WifiClient";

    f.print("    <Placemark>\n");
    f.print("      <name>");

    // Prefer tracker_type in name if present
    if (tt != TrackerType::Unknown) {
      f.print(BleTracker::TrackerTypeName(tt));
      f.print(" ");
    } else {
      f.print(kindStr);
      f.print(" ");
    }

    f.print(macStr);
    f.print("</name>\n");

    f.print("      <description>");
    f.print("Kind: ");
    f.print(kindStr);
    f.print("&#10;MAC: ");
    f.print(macStr);

    if (tt != TrackerType::Unknown) {
      f.print("&#10;TrackerType: ");
      f.print(BleTracker::TrackerTypeName(tt));
    }
    if (gm != GoogleFmnManufacturer::Unknown) {
      f.print("&#10;GoogleFMN: ");
      f.print(BleTracker::GoogleMfrName(gm));
    }
    if (ss != SamsungTrackerSubtype::Unknown) {
      f.print("&#10;SamsungSubtype: ");
      f.print(BleTracker::SamsungSubtypeName(ss));
    }
    if (tc != 0) {
      f.print("&#10;TrackerConfidence: ");
      f.print((unsigned)tc);
    }

    f.print("</description>\n");

    f.print("      <Point>\n");
    f.print("        <coordinates>");
    f.print(lon, 8);
    f.print(",");
    f.print(lat, 8);
    f.print(",0</coordinates>\n");
    f.print("      </Point>\n");
    f.print("    </Placemark>\n");

    wroteAny = true;
  }

  // Footer
  f.print("  </Document>\n");
  f.print("</kml>\n");

  // Ensure bytes hit the card before close (close typically flushes, but this is explicit)
  f.flush();
  f.close();

  Serial.printf("[kml] wrote %s (%s)\n", PATH_WATCHLIST_KML, wroteAny ? "with placemarks" : "no geo items");

  SD.end();

  return true;
}
