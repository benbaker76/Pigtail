#include "DeviceTracker.h"

#include <WiFi.h>
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

static constexpr int FP_TOP_N = 8;

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

// ----------------------------- Model -----------------------------

enum class TrackKind : uint8_t { WifiClient = 1, BleAdv = 2 };

struct Track {
  bool      in_use = false;
  TrackKind kind{};
  uint8_t   addr[6]{};

  Vendor    vendor = Vendor::Unknown;

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

  bool      tracker_like = false; // conservative placeholder
};

struct Anchor {
  bool     in_use = false;
  uint8_t  addr[6]{};

  Vendor   vendor = Vendor::Unknown;

  uint8_t  ssid[32]{};
  uint8_t  ssid_len = 0;

  uint16_t index = 0;
  int      last_rssi = -100;
  uint32_t last_seen_s = 0;

  // Geo tag
  bool     has_geo = false;

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
};

static QueueHandle_t g_obs_q = nullptr;

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
      t.index = g_next_index++;
      t.first_seen_s = ts_s;
      t.last_seen_s  = ts_s;
      t.last_segment_id = g_segment_id;
      t.env_hits = 1;
      return &t;
    }
  }
  // evict oldest
  int ev = 0;
  uint32_t oldest = UINT32_MAX;
  for (int i = 0; i < MAX_TRACKS; i++) {
    if (g_tracks[i].in_use && g_tracks[i].last_seen_s < oldest) { oldest = g_tracks[i].last_seen_s; ev = i; }
  }
  Track& t = g_tracks[ev];
  t = Track{};
  t.in_use = true;
  t.kind = kind;
  memcpy(t.addr, addr, 6);
  t.vendor = GetVendor(addr);
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
      a.index = g_next_index++;
      a.last_seen_s = ts_s;
      a.last_rssi = -100;
      return &a;
    }
  }
  // evict oldest
  int ev = 0;
  uint32_t oldest = UINT32_MAX;
  for (int i = 0; i < MAX_ANCHORS; i++) {
    if (g_anchors[i].in_use && g_anchors[i].last_seen_s < oldest) { oldest = g_anchors[i].last_seen_s; ev = i; }
  }
  Anchor& a = g_anchors[ev];
  a = Anchor{};
  a.in_use = true;
  memcpy(a.addr, bssid, 6);
  a.vendor = GetVendor(bssid);
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
  Tmp tmp[MAX_ANCHORS];
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

  std::sort(tmp, tmp + n, [](const Tmp& a, const Tmp& b){ return a.rssi > b.rssi; });

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
    uint32_t idle = ts_s - g_tracks[i].last_seen_s;
    uint32_t limit = (g_tracks[i].kind == TrackKind::WifiClient) ? TRACK_IDLE_SEC_WIFI : TRACK_IDLE_SEC_BLE;
    if (idle > limit) g_tracks[i].in_use = false;
  }
  for (int i = 0; i < MAX_ANCHORS; i++) {
    if (!g_anchors[i].in_use) continue;
    if (ts_s - g_anchors[i].last_seen_s > (uint32_t)ANCHOR_IDLE_SEC) g_anchors[i].in_use = false;
  }

  portEXIT_CRITICAL(&g_lock);
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
      update_track_from_obs(*t, obs.rssi_dbm, obs.ts_s);
    } break;

    case ObsKind::BleAdv: {
      Track* t = find_or_alloc_track(TrackKind::BleAdv, obs.addr, obs.ts_s);
      update_track_from_obs(*t, obs.rssi_dbm, obs.ts_s);
      // tracker_like stays conservative false until you add a classifier
    } break;

    case ObsKind::WifiApBeacon:
    case ObsKind::WifiApProbeResp: {
      Anchor* a = find_or_alloc_anchor(obs.addr, obs.ts_s);
      a->last_seen_s = obs.ts_s;
      a->last_rssi   = obs.rssi_dbm;

      if (obs.ssid_len > 0) {
        uint8_t ncopy = (uint8_t)std::min<size_t>(obs.ssid_len, sizeof(a->ssid));
        a->ssid_len = ncopy;
        if (ncopy) memcpy(a->ssid, obs.ssid, ncopy);
      }

      // If GPS fix available, geo-tag this anchor
      if (g_gps_valid) {
        // best-pass update
        if (!a->has_geo || obs.rssi_dbm > a->best_rssi) {
          a->has_geo = true;
          a->best_rssi = obs.rssi_dbm;
          a->best_lat = gps_lat;
          a->best_lon = gps_lon;
        }

        // weighted average (favor stronger signals)
        // map RSSI (-95..-35) => weight (1..10)
        float w = 1.0f + 9.0f * clamp01(((float)obs.rssi_dbm + 95.0f) / 60.0f);
        a->w_sum += (double)w;
        a->w_lat += (double)w * gps_lat;
        a->w_lon += (double)w * gps_lon;
        a->has_geo = true;
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

class ScanCB : public NimBLEAdvertisedDeviceCallbacks {
public:
  void onResult(NimBLEAdvertisedDevice* dev) override {
    Observation obs{};
    obs.kind = ObsKind::BleAdv;
    obs.ts_s = now_s();
    obs.rssi_dbm = (int8_t)dev->getRSSI();

    NimBLEAddress a = dev->getAddress();
    memcpy(obs.addr, a.getNative(), 6);

    auto name = dev->getName();
    size_t ncopy = std::min<size_t>(name.length(), sizeof(obs.ssid));
    obs.ssid_len = (uint8_t)ncopy;
    if (ncopy) memcpy(obs.ssid, name.c_str(), ncopy);

    xQueueSend(g_obs_q, &obs, 0);
  }
};

static NimBLEScan* g_scan = nullptr;

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

static void init_wifi_sniffer() {
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

static void init_ble_scan() {
  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  g_scan = NimBLEDevice::getScan();
  g_scan->setAdvertisedDeviceCallbacks(new ScanCB(), false);
  g_scan->setActiveScan(true);
  g_scan->setInterval(45);
  g_scan->setWindow(15);
  g_scan->setDuplicateFilter(false);
  g_scan->start(0, nullptr, false);

  Serial.println("BLE sniffer started");
}

// ----------------------------- DeviceTracker API -----------------------------

bool DeviceTracker::begin() {
  Serial.println("DeviceTracker starting...");
  //g_salt ^= (uint64_t)esp_timer_get_time();

  g_obs_q = xQueueCreate(256, sizeof(Observation));
  if (!g_obs_q) return false;

  init_wifi_sniffer();
  init_ble_scan();

  xTaskCreatePinnedToCore(processing_task, "dt_proc", 8192, nullptr, 10, nullptr, 0);
  xTaskCreatePinnedToCore(wifi_hop_task, "dt_hop",  4096, nullptr,  6, nullptr, 0);

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
    e.rssi = (int)lroundf(t.ema_rssi);
    e.age_s = (t.last_seen_s - t.first_seen_s);
    e.last_seen_s = t.last_seen_s;
    e.env_hits = t.env_hits;
    e.seen_windows = t.seen_windows;
    e.near_windows = t.near_windows;
    e.crowd = t.crowd_ema;
    e.score = score_track(t, stationary_ratio);
    e.tracker_like = t.tracker_like;

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
    e.ssid_len = std::min<uint8_t>(a.ssid_len, sizeof(e.ssid));
    if (e.ssid_len) memcpy(e.ssid, a.ssid, e.ssid_len);
    e.rssi = a.last_rssi;
    e.age_s = (ts - a.last_seen_s);
    e.last_seen_s = a.last_seen_s;

    e.score = 0.0f;           // anchors not “suspicious” by default
    e.tracker_like = false;

    e.has_geo = a.has_geo;
    if (a.has_geo) {
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
    if (a.score != b.score) return a.score > b.score;
    return a.rssi > b.rssi;
  });

  _segment_id = g_segment_id;
  _move_segments = g_move_segments;
  _last_env_tick_s = g_last_env_tick_s;

  return n;
}

void DeviceTracker::reset()
{
  // Clear pending observations so we don't immediately repopulate from old data.
  if (g_obs_q) {
    xQueueReset(g_obs_q);
  }

  portENTER_CRITICAL(&g_lock);

  // Clear tracked state
  memset(g_tracks,  0, sizeof(g_tracks));
  memset(g_anchors, 0, sizeof(g_anchors));
  g_next_index = 1;

  // Reset env segmentation / movement stats
  g_last_fp = EnvFingerprint{};
  g_last_env_tick_s = 0;
  g_segment_id = 1;
  g_move_segments = 0;

  // Reset crowd window
  g_current_window = 0;
  g_window_unique_hits = 0;

  // Reset GPS segmentation anchor (keep current GPS fix validity as-is)
  g_gps_anchor_valid = false;
  g_last_gps_seg_s = 0;

  portEXIT_CRITICAL(&g_lock);

  // Expose reset stats
  _segment_id = g_segment_id;
  _move_segments = g_move_segments;
  _last_env_tick_s = g_last_env_tick_s;
}
