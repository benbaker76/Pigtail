#include "GNSSModule.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"

// Create global GNSS module instance
GNSSModule gnssModule;

static constexpr unsigned long DEBUG_INTERVAL_MS = 5000;

// Constructor
GNSSModule::GNSSModule() 
    : gpsSerial(nullptr), 
      lastUpdateTime(0),
      isInitialized(false),
      rxPin(1),
      txPin(2),
      baudRate(9600) {
}

// Destructor
GNSSModule::~GNSSModule() {
    if (gpsSerial) {
        gpsSerial->end();
        delete gpsSerial;
    }
}

// Initialize GPS module
void GNSSModule::begin(uint32_t baud, int rx, int tx) {
    baudRate = baud;
    rxPin = rx;
    txPin = tx;
    
    // Create and configure hardware serial for GPS
    gpsSerial = new HardwareSerial(2);  // Use UART2
    gpsSerial->setRxBufferSize(4096);   // 2–8 KB is reasonable
    gpsSerial->begin(baudRate, SERIAL_8N1, rxPin, txPin);
    
    isInitialized = true;
    lastUpdateTime = millis();
    
    Serial.print("GNSS Module initialized on UART2 - RX:");
    Serial.print(rxPin);
    Serial.print(" TX:");
    Serial.print(txPin);
    Serial.print(" Baud:");
    Serial.println(baudRate);
    Serial.println("NOTE: GPS modules may need 30-60s for first fix with clear sky view");

    // Start GPS in the background:
    xTaskCreatePinnedToCore(&GNSSModule::taskThunk, "gnss_task",
                        4096, this, 5, &_task, 1);
}

void GNSSModule::taskThunk(void* arg) {
  static_cast<GNSSModule*>(arg)->taskLoop();
}

GnssFixSnapshot GNSSModule::snapshot() const {
  GnssFixSnapshot out;
  portENTER_CRITICAL(&_mux);
  out = _snap;
  portEXIT_CRITICAL(&_mux);
  return out;
}

void GNSSModule::taskLoop() {
  uint32_t lastLogMs = 0;

  for (;;) {
    // Drain UART frequently
    while (gpsSerial && gpsSerial->available() > 0) {
      char c = (char)gpsSerial->read();
      gps.encode(c);
    }

    // Update snapshot ~5–10 Hz is plenty
    const uint32_t nowMs = millis();
    if ((int32_t)(nowMs - _snap.last_update_ms) >= 200) {
      GnssFixSnapshot s{};
      s.valid = gps.location.isValid();
      if (s.valid) {
        s.lat = gps.location.lat();
        s.lon = gps.location.lng();
      }
      s.sats = gps.satellites.isValid() ? gps.satellites.value() : 0;
      s.speed_kmph = gps.speed.isValid() ? gps.speed.kmph() : 0.0;
      s.course_deg = gps.course.isValid() ? gps.course.deg() : 0.0;
      s.alt_m = gps.altitude.isValid() ? gps.altitude.meters() : 0.0;
      s.last_update_ms = nowMs;

      portENTER_CRITICAL(&_mux);
      _snap = s;
      portEXIT_CRITICAL(&_mux);
    }

    // Optional: very throttled logging
    if ((int32_t)(nowMs - lastLogMs) >= DEBUG_INTERVAL_MS) {
      lastLogMs = nowMs;
      Serial.printf("[gps] valid=%d sats=%d chars=%lu pass=%lu fail=%lu\n",
                    (int)gps.location.isValid(),
                    gps.satellites.isValid() ? gps.satellites.value() : 0,
                    (unsigned long)gps.charsProcessed(),
                    (unsigned long)gps.passedChecksum(),
                    (unsigned long)gps.failedChecksum());
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // yield
  }
}

// Get GPS data functions
double GNSSModule::getLatitude() {
    if (gps.location.isValid()) {
        return gps.location.lat();
    }
    return 0.0;
}

double GNSSModule::getLongitude() {
    if (gps.location.isValid()) {
        return gps.location.lng();
    }
    return 0.0;
}

int GNSSModule::getSatellites() {
    if (gps.satellites.isValid()) {
        return gps.satellites.value();
    }
    return 0;
}

double GNSSModule::getSpeed() {
    if (gps.speed.isValid()) {
        return gps.speed.kmph();
    }
    return 0.0;
}

double GNSSModule::getCourse() {
    if (gps.course.isValid()) {
        return gps.course.deg();
    }
    return 0.0;
}

double GNSSModule::getAltitude() {
    if (gps.altitude.isValid()) {
        return gps.altitude.meters();
    }
    return 0.0;
}

bool GNSSModule::isValid() {
    return gps.location.isValid();
}

// Format coordinate for display
String GNSSModule::formatCoordinate(double coord, bool isLatitude) {
    char buffer[20];
    char direction;
    
    if (isLatitude) {
        direction = (coord >= 0) ? 'N' : 'S';
    } else {
        direction = (coord >= 0) ? 'E' : 'W';
    }
    
    double absCoord = abs(coord);
    snprintf(buffer, sizeof(buffer), "%.6f %c", absCoord, direction);
    return String(buffer);
}

// Get formatted strings for display
String GNSSModule::getFormattedLatitude() {
    if (gps.location.isValid()) {
        return formatCoordinate(gps.location.lat(), true);
    }
    return "---";
}

String GNSSModule::getFormattedLongitude() {
    if (gps.location.isValid()) {
        return formatCoordinate(gps.location.lng(), false);
    }
    return "---";
}

String GNSSModule::getFormattedSpeed() {
    if (gps.speed.isValid()) {
        char buffer[20];
        snprintf(buffer, sizeof(buffer), "%.1f km/h", gps.speed.kmph());
        return String(buffer);
    }
    return "-- km/h";
}

String GNSSModule::getFormattedAltitude() {
    if (gps.altitude.isValid()) {
        char buffer[20];
        snprintf(buffer, sizeof(buffer), "%.1f m", gps.altitude.meters());
        return String(buffer);
    }
    return "--- m";
}

String GNSSModule::getFormattedCourse() {
    if (gps.course.isValid()) {
        char buffer[20];
        snprintf(buffer, sizeof(buffer), "%.0f°", gps.course.deg());
        return String(buffer);
    }
    return "---°";
}

String GNSSModule::getFormattedSatellites() {
    if (gps.satellites.isValid()) {
        return String(gps.satellites.value());
    }
    return "0";
}

String GNSSModule::getFormattedDateTime() {
    if (gps.date.isValid() && gps.time.isValid()) {
        char buffer[30];
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
                gps.date.year(), gps.date.month(), gps.date.day(),
                gps.time.hour(), gps.time.minute(), gps.time.second());
        return String(buffer);
    }
    return "----/--/-- --:--:--";
}

// Global function implementations
void gnss_begin(uint32_t baud, int rx, int tx) {
    gnssModule.begin(baud, rx, tx);
}
