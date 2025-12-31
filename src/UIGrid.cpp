// UIGrid.cpp
#include "UIGrid.h"

#include "Colors.h"
#include "Icons.h"

#include "Icon.h"
#include "RetroAvatar.h"
#include "Names.h"
#include "MarkovNameGenerator.h"

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
  static inline void formatMac(const uint8_t *mac, char *out) {
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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

  // RGB565 -> RGB332
  static inline uint8_t rgb565_to_rgb332(uint16_t c)
  {
    const uint8_t r5 = (c >> 11) & 0x1F;
    const uint8_t g6 = (c >> 5)  & 0x3F;
    const uint8_t b5 = (c >> 0)  & 0x1F;

    const uint8_t r3 = r5 >> 2;   // 5->3
    const uint8_t g3 = g6 >> 3;   // 6->3
    const uint8_t b2 = b5 >> 3;   // 5->2

    return (uint8_t)((r3 << 5) | (g3 << 2) | b2);
  }

  static std::array<uint8_t, 16> BuildPico332()
  {
    std::array<uint8_t, 16> t{};
    for (size_t i = 0; i < 16; ++i)
      t[i] = rgb565_to_rgb332(Colors::Pico8Colors[i]);
    return t;
  }

  static const std::array<uint8_t, 16> g_pico332 = BuildPico332();

  // Access Icon's rendered indexed pixels (palette indices 0..15).
  // Requires Icon.h to expose Pixels(), ImageW(), ImageH().
  static const uint8_t* GetIconPixels32(const Icon& icon, int& outW, int& outH)
  {
    outW = icon.ImageW();
    outH = icon.ImageH();
    return icon.Pixels().data();
  }

  // Blit indexed (0..15) to 8-bit RGB332 sprite.
  static inline void blitIndexedToSprite8(
    uint8_t* dst8, int dstW, int dstH,
    int dstX, int dstY,
    const uint8_t* srcIdx, int srcW, int srcH,
    const std::array<uint8_t,16>& pal332
  )
  {
    if (!dst8 || !srcIdx) return;

    const int x0 = std::max(0, dstX);
    const int y0 = std::max(0, dstY);
    const int x1 = std::min(dstW, dstX + srcW);
    const int y1 = std::min(dstH, dstY + srcH);
    if (x0 >= x1 || y0 >= y1) return;

    for (int y = y0; y < y1; ++y)
    {
      const int sy = y - dstY;
      const int dstRow = y * dstW;
      const int srcRow = sy * srcW;

      for (int x = x0; x < x1; ++x)
      {
        const int sx = x - dstX;
        uint8_t idx = srcIdx[srcRow + sx] & 0x0F;

        dst8[dstRow + x] = pal332[idx & 0x0F];
      }
    }
  }
}

// ------------------------------------------------------------

UIGrid::UIGrid(const std::string &version) : _version(version) {}
UIGrid::~UIGrid() { destroySprite(); }

float UIGrid::clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Map RSSI (-95..-35) to 0..1
float UIGrid::rssiTo01(int rssiDbm)
{
  const float lo = -95.0f;
  const float hi = -35.0f;
  return clamp01(((float)rssiDbm - lo) / (hi - lo));
}

Icons::LvglSymbol UIGrid::typeToLvglSymbol(EntityKind kind)
{
  switch (kind)
  {
    case EntityKind::BleAdv:      return Icons::LvglSymbol::Bluetooth;
    case EntityKind::WifiClient:  return Icons::LvglSymbol::Wifi;
    case EntityKind::WifiAp:      return Icons::LvglSymbol::Home;
    default:                      return Icons::LvglSymbol::Home;
  }
}

// Pico-8 palette indices: BLUE=12, GREEN=11, LAVENDER=13
uint8_t UIGrid::typeToPicoColorIndex(EntityKind kind)
{
  switch (kind)
  {
    case EntityKind::BleAdv:      return 12; // Blue
    case EntityKind::WifiClient:  return 11; // Green
    case EntityKind::WifiAp:      return 9;  // Orange
    default:                      return 7;  // White
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
  _w = lcd.width();
  _h = lcd.height();

  destroySprite();

  _spr = new LGFX_Sprite(&lcd);
  if (!_spr) {
    Serial.println("[UI] Sprite new failed");
    return;
  }

  _spr->setColorDepth(8);     // RGB332
  _spr->setTextWrap(false);
  _spr->setTextSize(1);

  _spr->createSprite(_w, _h);

  if (_spr->getBuffer() == nullptr) {
    Serial.printf("[UI] Sprite alloc failed (%dx%d). free=%u\n", _w, _h, (unsigned)ESP.getFreeHeap());
    destroySprite();
    return;
  }

  _buf8 = (uint8_t*)_spr->getBuffer();
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
  _buf8 = nullptr;
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

  const bool enter = ks.enter;

  const bool esc   = kb.isKeyPressed((char)0x1B);
  const bool back  = ks.del || kb.isKeyPressed('`') || esc;

  // Space cycles icon mode (grid view only)
  const bool space = ks.space || kb.isKeyPressed(' ');
  if (_screen == Screen::Grid && space) {
    cycleGridIconMode();
    // do NOT return; allow other keys in same event (rare, but harmless)
  }

  // Navigation: use isKeyPressed so we do not depend on ks.word being populated.
  const bool up    = kb.isKeyPressed(';') || kb.isKeyPressed(':');
  const bool down  = kb.isKeyPressed('.') || kb.isKeyPressed('>');
  const bool left  = kb.isKeyPressed(',') || kb.isKeyPressed('<');
  const bool right = kb.isKeyPressed('/') || kb.isKeyPressed('?');

  if (_screen == Screen::Grid) {

    // ESC or BACK in GRID = home (top-left). In detail it remains "back".
    if (esc || back) {
      _offset = 0;
      setSelectionSlot(0);
      return;
    }

    if      (up)    nav(0, -1);
    else if (down)  nav(0, +1);
    else if (left)  nav(-1, 0);
    else if (right) nav(+1, 0);
    else if (enter) openDetail();

  } else {

    // detail view
    if (enter || back) {
      closeDetail();
    } else {
      if      (up)    nav(0, -1);
      else if (down)  nav(0, +1);
      else if (left)  nav(-1, 0);
      else if (right) nav(+1, 0);
    }
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

void UIGrid::drawDetail()
{
  createSprite();
  if (!_sprInit) return;

  _spr->fillScreen(C_BLACK);
  _spr->setTextColor(C_LIGHT_GREY, C_BLACK);

  // Find locked detail device in live snapshot
  const EntityView* pe = nullptr;
  if (_detail_index_valid) {
    for (int i = 0; i < _count; ++i) {
      if (_items[i].index == _detail_index && _items[i].kind == _detail_kind) {
        pe = &_items[i];
        break;
      }
    }
  }

  if (!pe) {
    _spr->setCursor(4, 0);
    _spr->print("Not found.");
    _spr->setCursor(4, 12);
    _spr->print("Enter/Del/Esc: back");
    pushFrame();
    return;
  }

  const EntityView& e = *pe;

  // ---- Type icon + color (BT=blue, WiFi=green, AP=lavender) ----
  const Icons::LvglSymbol sym = typeToLvglSymbol(e.kind);

  uint32_t id = HashMac32_Fnv1a(e.addr);

  // ---- Retro name ----
  MarkovNameGenerator gen(Names::Hindu(), id, 1, 4, 8);
  const std::string name = ToUpper(gen.NextName());

  // ---- Header requested layout ----
  renderIcon16ToSprite(4, 4, sym, typeToPicoColorIndex(e.kind));

  _spr->setTextSize(2);                 // name bigger
  _spr->setCursor(25, 5);
  _spr->printf("%s", name.c_str());

  _spr->setTextColor(C_WHITE, C_BLACK);
  _spr->setTextSize(1);

  int textX = 4;
  int textY = 28;

  char mac[18] = {};
  formatMac(e.addr, mac);

  _spr->setCursor(textX, textY);
  _spr->printf("MAC: %s", mac);
  textY += 12;

  _spr->setCursor(textX, textY);
  _spr->printf("RSSI: %ddBm", (int)e.rssi);
  textY += 12;

  _spr->setCursor(textX, textY);
  _spr->printf("Score: %.1f", (double)e.score);
  textY += 12;

  if (e.has_geo) {
    _spr->setCursor(textX, textY);
    _spr->printf("Lat: %.6f", e.lat);
    textY += 12;

    _spr->setCursor(textX, textY);
    _spr->printf("Lon: %.6f", e.lon);
    textY += 12;
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

  pushFrame();
}

// ------------------------------------------------------------
// Icon rendering
// ------------------------------------------------------------

void UIGrid::renderGridIconToSprite(int dstX, int dstY, const EntityView& e)
{
  if (!_buf8) return;

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

  const Icons::LvglSymbol symbol = typeToLvglSymbol(e.kind);
  const uint8_t typeColor = typeToPicoColorIndex(e.kind);

  Icons::LvglSymbol small1 = e.has_geo ? Icons::LvglSymbol::Gps : Icons::LvglSymbol::Bullet;
  Icons::LvglSymbol small2 = Icons::LvglSymbol::Bullet;

  std::uint8_t bar1ColorIndex = 12;
  std::uint8_t bar2ColorIndex = 13;

  std::uint8_t largeIconColorIndex = 12;
  std::uint8_t smallIcon1ColorIndex = 12;
  std::uint8_t smallIcon2ColorIndex = 12;

  if (iconType == Icon::IconType::RetroAvatarWithMac)
  {
    small2 = symbol;
    smallIcon2ColorIndex = typeColor;
  }
  else if (iconType == Icon::IconType::LargeIconWithMac)
  {
    largeIconColorIndex = typeColor;
  }

  // If EntityView has MAC, replace here
  char mac[18] = {};
  formatMac(e.addr, mac);
  std::string macStr = std::string(mac);

  uint32_t id = HashMac32_Fnv1a(e.addr);

  Icon icon(id, mac);
  icon.DrawIcon(iconType,
    bar1,
    bar1ColorIndex,
    bar2,
    bar2ColorIndex,
    symbol,
    largeIconColorIndex,
    small1,
    smallIcon1ColorIndex,
    small2,
    smallIcon2ColorIndex
  );

  int iw=0, ih=0;
  const uint8_t* src = GetIconPixels32(icon, iw, ih);
  if (!src || iw <= 0 || ih <= 0) return;

  blitIndexedToSprite8(
    _buf8, _w, _h,
    dstX, dstY,
    src, iw, ih,
    g_pico332
  );
}

void UIGrid::renderDetailAvatar48(int dstX, int dstY, uint32_t id)
{
  if (!_buf8) return;

  // Render a 48x48 avatar at SCALE_4X into a ByteGrid, then blit to sprite.
  // This assumes you have ByteGrid available (same type used by Icon.cpp).
  static constexpr int AW = 48;
  static constexpr int AH = 48;

  ByteGrid grid;
  grid.Reset(AW, AH);

  RetroAvatar ra(id);
  ra.GenerateAvatar();
  ra.DrawAvatar(grid, 0, 0, SCALE_4X);

  blitIndexedToSprite8(
    _buf8, _w, _h,
    dstX, dstY,
    grid.Raw().data(), AW, AH,
    g_pico332
  );
}

void UIGrid::renderIcon16ToSprite(int dstX, int dstY, Icons::LvglSymbol sym, uint8_t picoColorIndex)
{
  if (!_buf8) return;

  const uint8_t* iconData = Icons::Get16x16(sym);
  if (!iconData) return;

  const int width = 16;
  const int height = 16;
  const int bytesPerRow = 2; // 16 bits

  const uint8_t c332 = g_pico332[picoColorIndex & 0x0F];

  const int x0 = std::max(0, dstX);
  const int y0 = std::max(0, dstY);
  const int x1 = std::min(_w, dstX + width);
  const int y1 = std::min(_h, dstY + height);

  for (int y = y0; y < y1; ++y)
  {
    const int sy = y - dstY;
    const int rowBase = sy * bytesPerRow;
    const int dstRow = y * _w;

    for (int x = x0; x < x1; ++x)
    {
      const int sx = x - dstX;
      const int byteIndex = rowBase + (sx >> 3);
      const int bitIndex  = (sx & 7); // LSB-first (XBM)

      const bool on = ((iconData[byteIndex] >> bitIndex) & 1u) != 0;
      if (on) _buf8[dstRow + x] = c332;
    }
  }
}
