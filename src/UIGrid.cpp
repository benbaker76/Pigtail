// UIGrid.cpp
#include "UIGrid.h"

#include "Colors.h"
#include "Icons.h"

#include "Icon.h"
#include "RetroAvatar.h"
#include "Names.h"
#include "MarkovNameGenerator.h"
#include "MacPrefixes.h"
#include "BleTracker.h"
#include "Track.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>

// ------------------------------------------------------------
// Small helpers
// ------------------------------------------------------------
namespace
{
  static inline void formatMac(const uint8_t mac[6], char out[18])
  {
      if (!mac || !out) return;

      static constexpr char kHex[] = "0123456789ABCDEF";

      int j = 0;
      for (int i = 0; i < 6; ++i)
      {
          const uint8_t b = mac[i];
          out[j++] = kHex[(b >> 4) & 0x0F];
          out[j++] = kHex[b & 0x0F];
          if (i != 5) out[j++] = ':';
      }
      out[j] = '\0';
  }

  static inline uint32_t HashMac32_Fnv1a(const uint8_t addr[6])
  {
      // FNV-1a 32-bit
      uint32_t h = 2166136261u;
      for (int i = 0; i < 6; ++i) {
          h ^= (uint32_t)addr[i];
          h *= 16777619u;
      }

      if (h == 0) h = 1;

      return h;
  }

  static std::string ToUpper(std::string s)
  {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
  }
};

// ------------------------------------------------------------

UIGrid::UIGrid(const std::string &version) : _version(version){}
UIGrid::~UIGrid() { destroySprite(); }

float UIGrid::clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Map RSSI (-95..-35) to 0..1
float UIGrid::rssiTo01(int rssiDbm)
{
  const float lo = -95.0f;
  const float hi = -35.0f;
  return clamp01(((float)rssiDbm - lo) / (hi - lo));
}

Icons::IconSymbol UIGrid::typeToIconSymbol(EntityKind kind)
{
  switch (kind)
  {
    case EntityKind::BleAdv:      return Icons::IconSymbol::Bluetooth;
    case EntityKind::WifiClient:  return Icons::IconSymbol::Wifi;
    case EntityKind::WifiAp:      return Icons::IconSymbol::AccessPoint;
    default:                      return Icons::IconSymbol::None;
  }
}

// Pico-8 palette indices: BLUE=12, GREEN=11, LAVENDER=13
uint8_t UIGrid::typeToPicoColorIndex(EntityKind kind)
{
  switch (kind)
  {
    case EntityKind::BleAdv:      return C_BLUE;
    case EntityKind::WifiClient:  return C_GREEN;
    case EntityKind::WifiAp:      return C_ORANGE;
    default:                      return C_WHITE;
  }
}

const char* UIGrid::typeToName(EntityKind kind)
{
  switch (kind)
  {
    case EntityKind::BleAdv:      return "BLE";
    case EntityKind::WifiClient:  return "WIFI";
    case EntityKind::WifiAp:      return "AP";
    default:                      return "UNK";
  }
}

// ------------------------------------------------------------

void UIGrid::begin(DeviceTracker* tracker)
{
  _tracker = tracker;

  createSprite();

  _offset = 0;
  _sel_slot = 0;
  _sel_idx = -1;
  _sel_index_valid = false;
  _detail_index_valid = false;

  setSelectionSlot(0);
}
void UIGrid::createSprite()
{
  if (_sprInit) return;

  auto& lcd = M5Cardputer.Display;
  
  lcd.setColorDepth(8); // RGB332

  _w = lcd.width();
  _h = lcd.height();

  destroySprite();

  _spr = new LGFX_Sprite(&lcd);
  if (!_spr) {
    Serial.println("[UI] Sprite new failed");
    return;
  }

  _spr->setColorDepth(lgfx::palette_4bit);
  _spr->setTextWrap(false);
  _spr->setTextSize(1);

  _spr->createSprite(_w, _h);
  _spr->createPalette((const uint16_t*)Colors::Pico8Colors, 16);

  if (_spr->getBuffer() == nullptr) {
    Serial.printf("[UI] Sprite alloc failed (%dx%d). free=%u\n", _w, _h, (unsigned)ESP.getFreeHeap());
    destroySprite();
    return;
  }

  _sprInit = true;

  Serial.printf("[UI] Sprite OK (%dx%d). bytes=%u free=%u\n",
                _w, _h, (unsigned)_spr->bufferLength(), (unsigned)ESP.getFreeHeap());
}

void UIGrid::destroySprite()
{
  if (_spr) {
    _spr->deleteSprite();
    delete _spr;
    _spr = nullptr;
  }
  _sprInit = false;
}

void UIGrid::pushFrame()
{
  if (!_sprInit) return;
  M5Cardputer.Display.startWrite();
  _spr->pushSprite(0, 0);
  M5Cardputer.Display.endWrite();
}

// ------------------------------------------------------------

void UIGrid::update(float stationary_ratio)
{
  (void)stationary_ratio; // currently not used; keep for future

  if (!_tracker) return;

  _count = _tracker->buildSnapshot(_items, 256, stationary_ratio);

  // Keep cursor on the same device as list updates
  syncSelectionToId();

  // Clamp offset in bounds (row-aligned so columns remain stable)
  int max_offset = 0;
  if (_count > SLOTS) {
    const int lastRow = (_count - 1) / COLS;
    const int firstVisibleRow = std::max(0, lastRow - (ROWS - 1));
    max_offset = firstVisibleRow * COLS;
  }
  if (_offset > max_offset) _offset = max_offset;
  if (_offset < 0) _offset = 0;

  // Recompute selection indices
  setSelectionSlot(_sel_slot);

  if (_screen == Screen::Detail) {
    lockDetailToSelection();
  }

  if (_screen == Screen::Grid) drawGrid();
  else                         drawDetail();
}

void UIGrid::setSelectionSlot(int slot)
{
  if (slot < 0) slot = 0;
  if (slot >= SLOTS) slot = SLOTS - 1;
  _sel_slot = slot;

  int idx = _offset + _sel_slot;
  _sel_idx = (idx >= 0 && idx < _count) ? idx : -1;

  if (_sel_idx >= 0) {
    _sel_index = _items[_sel_idx].index;
    _sel_kind = _items[_sel_idx].kind;
    _sel_index_valid = true;
  } else {
    _sel_index_valid = false;
  }
}

void UIGrid::syncSelectionToId()
{
  if (!_sel_index_valid || _count <= 0) return;

  int found = -1;
  for (int i = 0; i < _count; ++i) {
    if (_items[i].index == _sel_index && _items[i].kind == _sel_kind) {
      found = i;
      break;
    }
  }
  if (found < 0) return;

  // Keep same row/col visual position when possible
  const int selRow = _sel_slot / COLS;
  const int selCol = _sel_slot % COLS;

  int desiredOffset = (found / COLS) * COLS - selRow * COLS;

  int max_offset = (_count > SLOTS) ? (_count - SLOTS) : 0;
  desiredOffset = std::max(0, std::min(desiredOffset, max_offset));
  _offset = desiredOffset;

  int newSlot = found - _offset;

  // Clamp into visible grid; try to preserve column where possible
  if (newSlot < 0) newSlot = 0;
  if (newSlot >= SLOTS) {
    // attempt to land in same col on last visible row
    newSlot = (ROWS - 1) * COLS + selCol;
    if (newSlot >= SLOTS) newSlot = SLOTS - 1;
  }

  _sel_slot = newSlot;
}

// ------------------------------------------------------------

void UIGrid::toggleGridIconMode()
{
  const int m = (int)_gridMode;
  _gridMode = (GridIconMode)((m + 1) % 3);
}

void UIGrid::openDetail()
{
  if (_sel_idx < 0) return;

  lockDetailToSelection();

  _detail_index = _items[_sel_idx].index;
  _detail_kind = _items[_sel_idx].kind;
  _detail_index_valid = true;
  _screen = Screen::Detail;
}

void UIGrid::closeDetail()
{
  _detail_index_valid = false;
  _screen = Screen::Grid;
}

// ------------------------------------------------------------

void UIGrid::cycleGridIconMode()
{
  _gridMode = (GridIconMode)(((int)_gridMode + 1) % 3); // or however you store it
}

void UIGrid::handleKeyboard(Keyboard_Class& kb)
{
  const auto& ks = kb.keysState();

  const bool esc   = kb.isKeyPressed('`');
  const bool enter = ks.enter;
  const bool back  = ks.del;

  // ESC = full reset/clear list (works from either screen)
  if (esc) {
    if (_tracker) _tracker->reset();

    _screen = Screen::Grid;
    _detail_index_valid = false;
    _sel_index_valid = false;

    _offset = 0;
    setSelectionSlot(0);
    playSound(800, 100);
    return;
  }

  // Space cycles icon mode (grid view only)
  const bool space = ks.space || kb.isKeyPressed(' ');
  if (_screen == Screen::Grid && space) {
    cycleGridIconMode();
    playSound(600, 100);
    // do NOT return; allow other keys in same event (rare, but harmless)
  }

  // Navigation: use isKeyPressed so we do not depend on ks.word being populated.
  const bool up    = kb.isKeyPressed(';') || kb.isKeyPressed(':');
  const bool down  = kb.isKeyPressed('.') || kb.isKeyPressed('>');
  const bool left  = kb.isKeyPressed(',') || kb.isKeyPressed('<');
  const bool right = kb.isKeyPressed('/') || kb.isKeyPressed('?');
  const bool wKey  = kb.isKeyPressed('w') || kb.isKeyPressed('W');
  const bool kKey  = kb.isKeyPressed('k') || kb.isKeyPressed('K');

  switch(_screen) {
    case Screen::Grid:
      {
        // BACK in GRID = home (top-left). In detail it remains "back".
        if (back) {
          _offset = 0;
          setSelectionSlot(0);
          playSound(800, 100);
          return;
        }

        if      (up)    { nav(0, -1); playSound(800, 50); }
        else if (down)  { nav(0, +1); playSound(800, 50); }
        else if (left)  { nav(-1, 0); playSound(800, 50); }
        else if (right) { nav(+1, 0); playSound(800, 50); }
        else if (enter) { openDetail(); playSound(1000, 100); }
        else if (wKey) {
          EntityView * pe = getSelectedEntity();
          if (pe) {
            // Toggle "watching" flag
            if (HasFlag(pe->flags, EntityFlags::Watching)) {
                ClearFlag(pe->flags, EntityFlags::Watching);
              } else {
                SetFlag(pe->flags, EntityFlags::Watching);
              }
            _tracker->updateEntity(pe);
            _tracker->writeWatchlist();
            playSound(600, 100);
          }
        }
        else if (kKey) {
            _tracker->writeWatchlistKml();
            playSound(1000, 100);
        }
        
      } break;

    case Screen::Detail:
      {
        // detail view
        if (enter || back) {
          closeDetail();
          playSound(800, 100);
        } else {
          if      (up)    { nav(0, -1); playSound(800, 50); }
          else if (down)  { nav(0, +1); playSound(800, 50); }
          else if (left)  { nav(-1, 0); playSound(800, 50); }
          else if (right) { nav(+1, 0); playSound(800, 50); }
          else if (wKey) {
            EntityView* pe = getSelectedEntity();
            if (pe) {
              // Toggle "watching" flag
              if (HasFlag(pe->flags, EntityFlags::Watching)) {
                ClearFlag(pe->flags, EntityFlags::Watching);
              } else {
                SetFlag(pe->flags, EntityFlags::Watching);
              }
              _tracker->updateEntity(pe);
              _tracker->writeWatchlist();
              playSound(600, 100);
            }
          }
          else if (kKey) {
            _tracker->writeWatchlistKml();
            playSound(1000, 100);
          }
        }
      } break;

    default:
      break;
  }
}

void UIGrid::lockDetailToSelection()
{
  if (_sel_idx >= 0 && _sel_idx < _count) {
    _detail_index = _items[_sel_idx].index;
    _detail_kind = _items[_sel_idx].kind;
    _detail_index_valid = true;
  } else {
    _detail_index_valid = false;
  }
}

EntityView* UIGrid::getDetailEntity()
{
  // Find locked detail device in live snapshot
  EntityView* pe = nullptr;
  if (_detail_index_valid) {
    for (int i = 0; i < _count; ++i) {
      if (_items[i].index == _detail_index && _items[i].kind == _detail_kind) {
        pe = &_items[i];
        break;
      }
    }
  }

  return pe;
}

EntityView* UIGrid::getGridEntity()
{
  if (_sel_idx < 0 || _sel_idx >= _count)
    return nullptr;

  return &_items[_sel_idx];
}

EntityView* UIGrid::getSelectedEntity()
{
  if (_screen == Screen::Detail)
    return getDetailEntity();

  return getGridEntity();
}

void UIGrid::nav(int dx, int dy)
{
  if (_count <= 0) return;

  // Row-aligned max_offset so the grid stays stable.
  int max_offset = 0;
  if (_count > SLOTS) {
    const int lastRow = (_count - 1) / COLS;
    const int firstVisibleRow = std::max(0, lastRow - (ROWS - 1));
    max_offset = firstVisibleRow * COLS;
  }

  const int col = _sel_slot % COLS;
  const int row = _sel_slot / COLS;

  // -------------------------
  // Horizontal (no wrap)
  // -------------------------
  if (dx != 0 && dy == 0)
  {
    const int curIndex = _offset + _sel_slot;
    const int newIndex = curIndex + dx;

    if (newIndex < 0 || newIndex >= _count)
      return; // no wrap at ends

    // Ensure visible by scrolling whole rows.
    while (newIndex < _offset) {
      _offset = std::max(0, _offset - COLS);
    }
    while (newIndex >= _offset + SLOTS) {
      _offset = std::min(max_offset, _offset + COLS);
    }

    setSelectionSlot(newIndex - _offset);

    // If we're in detail view, follow selection.
    if (_screen == Screen::Detail) lockDetailToSelection();
    return;
  }

  // -------------------------
  // Vertical (row/col + scroll)
  // -------------------------
  if (dy != 0 && dx == 0)
  {
    int newOffset = _offset;
    int targetRow = row + dy;
    bool scrolled = false;

    if (targetRow < 0) {
      if (newOffset >= COLS) { newOffset -= COLS; scrolled = true; targetRow = 0; }
      else return; // top boundary, no wrap
    }
    else if (targetRow >= ROWS) {
      if (newOffset < max_offset) { newOffset += COLS; scrolled = true; targetRow = ROWS - 1; }
      else return; // bottom boundary, no wrap (THIS FIXES YOUR "JUMPS TO END")
    }

    int targetSlot  = targetRow * COLS + col;
    int targetIndex = newOffset + targetSlot;

    if (targetIndex >= _count) {
      // Only clamp to last item if we actually scrolled.
      if (!scrolled) return;

      const int lastIndex = _count - 1;
      newOffset = max_offset;                 // show the final page (row-aligned)
      targetIndex = lastIndex;
      targetSlot  = targetIndex - newOffset;  // slot on that final page
      if (targetSlot < 0) targetSlot = 0;
      if (targetSlot >= SLOTS) targetSlot = SLOTS - 1;
    }

    _offset = newOffset;
    setSelectionSlot(targetIndex - _offset);

    // If we're in detail view, follow selection.
    if (_screen == Screen::Detail) lockDetailToSelection();
    return;
  }
}

// ------------------------------------------------------------
// Drawing
// ------------------------------------------------------------

void UIGrid::drawGrid()
{
  createSprite();
  if (!_sprInit) return;

  _spr->fillScreen(C_BLACK);

  // Header at top; grid starts immediately below to fit 4 rows of 32 in 135px height.
  _spr->setTextColor(C_WHITE, C_BLACK);
  _spr->setCursor(4, 0);
  _spr->printf("Pigtail %s n=%d", _version.c_str(), _count);

  // Fit 4*32 = 128 into 135 with minimal vertical margin.
  // start_y = 7 -> 7 + 128 = 135 exactly. No padding between rows.
  const int pad = 0;

  const int gridW = COLS * TILE + (COLS - 1) * pad; // 224
  const int start_x = (_w - gridW) / 2;             // center (typically 8)
  const int start_y = 7;
  int sel_x = 0;
  int sel_y = 0;

  for (int slot = 0; slot < SLOTS; ++slot)
  {
    const int idx = _offset + slot;

    const int col = slot % COLS;
    const int row = slot / COLS;
    const int x = start_x + col * (TILE + pad);
    const int y = start_y + row * (TILE + pad);

    if (idx >= 0 && idx < _count)
      drawTile(slot, x, y);

    if (slot == _sel_slot) {
      sel_x = x;
      sel_y = y;
    }
  }

  // Draw selection
  _spr->drawRect(sel_x, sel_y, TILE, TILE, C_YELLOW);
  _spr->drawRect(sel_x - 1, sel_y - 1, TILE + 2, TILE + 2, C_YELLOW);

  pushFrame();
}

void UIGrid::drawTile(int slot, int x, int y)
{
  const int idx = _offset + slot;
  if (idx < 0 || idx >= _count) return;

  const auto& e = _items[idx];

  renderGridIconToSprite(x, y, e);
}

void UIGrid::playSound(int frequency, int duration)
{
  M5Cardputer.Speaker.tone(frequency, duration);
}

void UIGrid::drawDetail()
{
  createSprite();
  if (!_sprInit) return;

  _spr->fillScreen(C_BLACK);
  _spr->setTextColor(C_LIGHT_GREY, C_BLACK);

  const EntityView* pe = getSelectedEntity();

  if (!pe) {
    _spr->setTextSize(2);
    _spr->setCursor(4, 4);
    _spr->print("Not found.");

    _spr->setTextColor(C_LIGHT_GREY, C_BLACK);
    _spr->setTextSize(1);
  
    _spr->setCursor(4, _h - 12);
    _spr->print("Enter/Del/Esc: back");
    pushFrame();
    return;
  }

  const EntityView& e = *pe;

  // ---- Type icon + color (BT=blue, WiFi=green, AP=lavender) ----
  const uint8_t* iconData = Icons::Get16x16(typeToIconSymbol(e.kind));
  int iconColorIndex = typeToPicoColorIndex(e.kind);

  uint32_t id = HashMac32_Fnv1a(e.addr);

  bool isMacRandomized = IsMacRandomized(e.addr);
  std::string vendorStr = VendorToString(e.vendor);

  if (e.vendor != Vendor::Unknown) {
    iconData = Icons::Get16x16(e.vendor);
    iconColorIndex = Colors::VendorColorIndices[static_cast<int>(e.vendor)];
  }

  // ---- Retro name ----
  _icon.Reset(id);
  const std::string name = _icon.Name();

  // ---- Header requested layout ----
  renderIcon1bit16(4, 4, iconData, iconColorIndex);

  _spr->setTextSize(2);                 // name bigger
  _spr->setCursor(25, 5);
  _spr->printf("%s", name.c_str());

  _spr->setTextColor(C_WHITE, C_BLACK);
  _spr->setTextSize(1);

  int offX = 4;
  int offY = 28;

  char mac[18] = {};
  formatMac(e.addr, mac);

  if (e.ssid_len > 0) {
    char ssidPrintable[33] = {};
    memcpy(ssidPrintable, e.ssid, std::min((size_t)e.ssid_len, sizeof(ssidPrintable) - 1));
    _spr->setCursor(offX, offY);
    _spr->printf("SSID: %s", ssidPrintable);
    offY += 12;
  }

  if (e.vendor != Vendor::Unknown) {
    _spr->setCursor(offX, offY);
    _spr->printf("Vendor: %s", vendorStr.c_str());
    offY += 12;
  }

  if (e.tracker_type != TrackerType::Unknown) {
    const char* trackerTypeStr = BleTracker::TrackerTypeName(e.tracker_type);
    _spr->setCursor(offX, offY);

    if (e.tracker_google_mfr != GoogleFmnManufacturer::Unknown)
    {
      const char* googleMfrStr = BleTracker::GoogleMfrName(e.tracker_google_mfr);
      _spr->printf("Tracker: %s (%s)", trackerTypeStr, googleMfrStr);
    }
    else if (e.tracker_samsung_subtype != SamsungTrackerSubtype::Unknown)
    {
      const char* samsungSubtypeStr = BleTracker::SamsungSubtypeName(e.tracker_samsung_subtype);
      _spr->printf("Tracker: %s (%s)", trackerTypeStr, samsungSubtypeStr);
    }
    else
    {
      _spr->printf("Tracker: %s", trackerTypeStr);
    }

    offY += 12;
  }

  _spr->setCursor(offX, offY);
  _spr->printf("MAC: %s %s", mac, isMacRandomized ? "[R]" : "");
  offY += 12;

  _spr->setCursor(offX, offY);
  _spr->printf("RSSI: %ddBm", (int)e.rssi);
  offY += 12;

  _spr->setCursor(offX, offY);
  _spr->printf("Score: %.1f", (double)e.score);
  offY += 12;

  offY += 4;

  if (HasFlag(e.flags, EntityFlags::HasGeo)) {
    _spr->setCursor(offX, offY);
    _spr->printf("Lat: %.6f", e.lat);
    offY += 12;

    _spr->setCursor(offX, offY);
    _spr->printf("Lon: %.6f", e.lon);
    offY += 12;
  }

  // ---- Right side: avatar 48x48 top-right ----
  const int margin = 4;

  const int avatarW = 48;
  const int avatarH = 48;
  const int avatarX = _w - avatarW - margin;
  const int avatarY = margin;

  renderDetailAvatar48(avatarX, avatarY, id);

  // Footer
  _spr->setTextColor(C_LIGHT_GREY, C_BLACK);
  _spr->setCursor(4, _h - 12);
  _spr->print("Enter/Del/Esc: back");

  offY = _h - 12;

  // Right justified type
  const char* typeStr = typeToName(e.kind);
  _spr->setCursor(_w - 4 - (strlen(typeStr) * 6), offY);
  _spr->print(typeStr);

  offY -= 18;

  if (HasFlag(e.flags, EntityFlags::Watching)) {
    renderIcon1bit16(_w - 16 - 4, offY, Icons::Get16x16(Icons::IconSymbol::Watching), C_RED);
    offY -= 18;
  }

  if (e.tracker_type != TrackerType::Unknown) {
    renderIcon1bit16(_w - 16 - 4, offY, Icons::Get16x16(Icons::IconSymbol::Tracker), C_YELLOW);
    offY -= 18;
  }

  if (HasFlag(e.flags, EntityFlags::HasGeo)) {
    renderIcon1bit16(_w - 16 - 4, offY, Icons::Get16x16(Icons::IconSymbol::Gps), C_BLUE);
    offY -= 18;
  }

  pushFrame();
}

// ------------------------------------------------------------
// Icon rendering
// ------------------------------------------------------------

void UIGrid::renderGridIconToSprite(int dstX, int dstY, const EntityView& e)
{
  //if (!_buf8) return;

  Icon::IconType iconType = Icon::IconType::LargeIconWithMac;
  switch (_gridMode)
  {
    case GridIconMode::RetroAvatar:        iconType = Icon::IconType::RetroAvatar; break;
    case GridIconMode::RetroAvatarWithMac: iconType = Icon::IconType::RetroAvatarWithMac; break;
    case GridIconMode::LargeIconWithMac:   iconType = Icon::IconType::LargeIconWithMac; break;
  }

  // bar1Value must be 0..1 based on RSSI (no dB text overlay)
  const float bar1 = rssiTo01(e.rssi);

  // bar2Value: keep score normalized as a useful second metric
  const float bar2 = clamp01((float)e.score / 100.0f);

  // If EntityView has MAC, replace here
  char mac[18] = {};
  formatMac(e.addr, mac);
  std::string macStr = std::string(mac);

  bool isMacRandomized = IsMacRandomized(e.addr);
  std::string vendorStr = VendorToString(e.vendor);

  uint32_t id = HashMac32_Fnv1a(e.addr);

  const Icons::IconSymbol iconSymbol = typeToIconSymbol(e.kind);

  const std::uint8_t* largeIcon = e.vendor != Vendor::Unknown ? Icons::Get16x16(e.vendor) : Icons::Get16x16(iconSymbol);
  const uint8_t typeColor = typeToPicoColorIndex(e.kind);

  const std::uint8_t* small1Icon = Icons::Get8x8(Icons::IconSymbol::None);
  const std::uint8_t* small2Icon = Icons::Get8x8(Icons::IconSymbol::None);

  std::uint8_t bar1ColorIndex = C_BLUE;
  std::uint8_t bar2ColorIndex = C_LAVENDER;

  std::uint8_t largeIconColorIndex = C_BLUE;
  std::uint8_t smallIcon1ColorIndex = C_BLUE;
  std::uint8_t smallIcon2ColorIndex = C_BLUE;

  if (HasFlag(e.flags, EntityFlags::HasGeo))
  {
    small1Icon = Icons::Get8x8(Icons::IconSymbol::Gps);
  }

  if (HasFlag(e.flags, EntityFlags::Watching))
  {
    small1Icon = Icons::Get8x8(Icons::IconSymbol::Watching);
    smallIcon1ColorIndex = C_RED;
  }

  if (iconType == Icon::IconType::RetroAvatarWithMac)
  {
    small2Icon = Icons::Get8x8(iconSymbol);
    smallIcon2ColorIndex = typeColor;
  }
  else if (iconType == Icon::IconType::LargeIconWithMac)
  {
    if (e.vendor != Vendor::Unknown)
    {
      largeIconColorIndex = Colors::VendorColorIndices[static_cast<int>(e.vendor)];
      small2Icon = Icons::Get8x8(iconSymbol);
      smallIcon2ColorIndex = typeColor;
    }
    else
    {
      largeIconColorIndex = typeColor;
    }
  }

  if (e.tracker_type != TrackerType::Unknown)
  {
    small2Icon = Icons::Get8x8(Icons::IconSymbol::Tracker);
    smallIcon2ColorIndex = C_YELLOW;
  }

  _icon.Reset(id, mac);
  _icon.DrawIcon(iconType,
    bar1,
    bar1ColorIndex,
    bar2,
    bar2ColorIndex,
    largeIcon,
    largeIconColorIndex,
    small1Icon,
    smallIcon1ColorIndex,
    small2Icon,
    smallIcon2ColorIndex
  );

  _spr->pushImage(dstX, dstY, _icon.ImageW(), _icon.ImageH(), _icon.Pixels().data(), lgfx::palette_4bit, Colors::Pico8Colors);
}

void UIGrid::renderDetailAvatar48(int dstX, int dstY, uint32_t id)
{
  // Render a 48x48 avatar at SCALE_4X into a Indexed4bppImage, then blit to sprite.
  static constexpr int aw = 48;
  static constexpr int ah = 48;

  _grid.Reset(aw, ah);

  _icon.Reset(id);
  _icon.DrawAvatar(_grid, 0, 0, SCALE_4X);

  _spr->pushImage(dstX, dstY, aw, ah, _grid.Raw().data(), lgfx::palette_4bit, Colors::Pico8Colors);
}

static void expand1bpp_to_4bpp_16x16(uint8_t* out4bpp, const uint8_t* in1bpp, uint8_t onIndex)
{
  // 16x16 @4bpp = 128 bytes (2 pixels/byte)
  // Assume MSB-first within each byte: bit 7 is x=0, bit 0 is x=7
  for (int y = 0; y < 16; ++y)
  {
    const uint8_t* row = in1bpp + y * 2; // 16px => 2 bytes/row
    for (int x = 0; x < 16; ++x)
    {
      const int byteIndex = x >> 3;
      const int bit = 7 - (x & 7);
      const bool on = (row[byteIndex] >> bit) & 1;

      const uint8_t idx = on ? (onIndex & 0x0F) : 0;

      const int p = y * 16 + x;
      uint8_t& b = out4bpp[p >> 1];
      if (p & 1) b = (b & 0xF0) | idx;        // low nibble
      else       b = (idx << 4) | (b & 0x0F); // high nibble
    }
  }
}

void UIGrid::renderIcon1bit16(int dstX, int dstY, const uint8_t* iconData, uint8_t picoColorIndex)
{
  static uint8_t tmp4bpp[16 * 16 / 2] = {};
  expand1bpp_to_4bpp_16x16(tmp4bpp, iconData, picoColorIndex);

  // Treat index 0 as transparent if your pushImage overload supports it:
  _spr->pushImage(dstX, dstY, 16, 16, tmp4bpp, lgfx::palette_4bit, Colors::Pico8Colors);
}