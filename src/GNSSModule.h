#ifndef GNSSMODULE_H
#define GNSSMODULE_H

#include <Arduino.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>

struct GnssFixSnapshot {
  bool valid;
  double lat, lon;
  int sats;
  double speed_kmph;
  double course_deg;
  double alt_m;
  uint32_t last_update_ms;
};

// GNSS module class
class GNSSModule {
private:
    TinyGPSPlus gps;
    HardwareSerial* gpsSerial;
    uint32_t lastUpdateTime;
    bool isInitialized;
    
    // GPS UART configuration
    int rxPin;
    int txPin;
    uint32_t baudRate;
    
public:
    GNSSModule();
    ~GNSSModule();
    
    // Initialize GPS with specified pins and baud rate
    void begin(uint32_t baud = 9600, int rx = 1, int tx = 2);
    GnssFixSnapshot snapshot() const;

    // Get GPS data
    double getLatitude();
    double getLongitude();
    int getSatellites();
    double getSpeed();
    double getCourse();
    double getAltitude();
    bool isValid();
    
    // Get formatted strings
    String getFormattedLatitude();
    String getFormattedLongitude();
    String getFormattedSpeed();
    String getFormattedAltitude();
    String getFormattedCourse();
    String getFormattedSatellites();
    String getFormattedDateTime();
    
    // Get raw TinyGPS++ object
    TinyGPSPlus* getGPS() { return &gps; }
    
private:
    static void taskThunk(void* arg);
    void taskLoop();

    // shared state
    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    GnssFixSnapshot _snap{};
    TaskHandle_t _task = nullptr;

    String formatCoordinate(double coord, bool isLatitude);
};

// Global GNSS functions for compatibility
void gnss_begin(uint32_t baud, int rx, int tx);
void gnss_update();

// External GNSS module instance
extern GNSSModule gnssModule;

#endif // GNSSMODULE_H