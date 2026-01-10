#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "BleTracker.h"
#include "Track.h"
#include <FS.h>

class DeviceTracker {
public:
  bool begin(); // starts Wi-Fi sniffer + BLE scan + internal tasks
  void setGpsFix(bool valid, double lat, double lon); // optional; safe to call always

  void initBleScan();
  void stopBleScan();
  void restartBleScan();
  void initBleTracker();
  void initWifiSniffer();

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
  NimBLEScan* _bleScan;

  bool _sdAvailable = false;
  // internal state is in the .cpp
  uint32_t _segment_id = 1;
  uint32_t _move_segments = 0;
  uint32_t _last_env_tick_s = 0;
};
