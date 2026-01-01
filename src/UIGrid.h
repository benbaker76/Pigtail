// UIGrid.h
#pragma once

#include <cstdint>

#include <M5Cardputer.h>
#include "DeviceTracker.h"
#include "Icons.h"

class Icon;

class UIGrid
{
public:
  UIGrid(const std::string &version);
  ~UIGrid();

  void begin(DeviceTracker* tracker);
  void update(float stationary_ratio);
  void cycleGridIconMode();
  void handleKeyboard(Keyboard_Class& kb);

private:
  enum class Screen : uint8_t { Grid, Detail };

  enum class GridIconMode : uint8_t
  {
    RetroAvatar = 0,
    RetroAvatarWithMac = 1,
    LargeIconWithMac = 2,
  };

private:
  // 7x4 grid, 32x32 tiles
  static constexpr int COLS  = 7;
  static constexpr int ROWS  = 4;
  static constexpr int SLOTS = COLS * ROWS;
  static constexpr int TILE  = 32;

  // Sprite
  void createSprite();
  void destroySprite();
  void pushFrame();

  // Drawing
  void drawGrid();
  void drawDetail();
  void drawTile(int slot, int x, int y);

  // Icon rendering
  void renderGridIconToSprite(int dstX, int dstY, const EntityView& e);
  void renderDetailAvatar48(int dstX, int dstY, uint32_t id);
  void renderIcon16ToSprite(int dstX, int dstY, const uint8_t* iconData, uint8_t picoColorIndex);

  // Selection / navigation
  void setSelectionSlot(int slot);
  void syncSelectionToId();
  void lockDetailToSelection();
  void nav(int dx, int dy);
  void toggleGridIconMode();

  void openDetail();
  void closeDetail();

  static Icons::IconSymbol typeToIconSymbol(EntityKind kind);
  static uint8_t           typeToPicoColorIndex(EntityKind kind);
  static const char*       typeToName(EntityKind kind);

  static float rssiTo01(int rssiDbm);
  static float clamp01(float v);

private:
  std::string _version;

  DeviceTracker* _tracker = nullptr;

  EntityView _items[256]{};
  int        _count = 0;

  Screen       _screen = Screen::Grid;
  GridIconMode _gridMode = GridIconMode::LargeIconWithMac;

  int _offset = 0;
  int _sel_slot = 0;
  int _sel_idx = -1;

  // Lock cursor to the same device across list updates
  bool       _sel_index_valid = false;
  uint32_t   _sel_index = 0;
  EntityKind _sel_kind = EntityKind::WifiClient;

  // Detail locks to a device id too
  bool       _detail_index_valid = false;
  uint32_t   _detail_index = 0;
  EntityKind _detail_kind = EntityKind::WifiClient;

  // Sprite
  LGFX_Sprite* _spr = nullptr;
  bool _sprInit = false;
  int  _w = 0;
  int  _h = 0;
  uint8_t* _buf8 = nullptr; // RGB332 sprite buffer
};
