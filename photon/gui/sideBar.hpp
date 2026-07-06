#pragma once
#include <mutex>
#include <string>

struct GUI;
struct TitleBar;
struct Canvas;
struct Tabs;
struct ImTextureData;

struct Sidebar {
  float width = 360.0f;
  float storedWidth = 360.0f;
  ImTextureData* backgroundTexture = nullptr;
  std::mutex dbcDialogMutex{};
  std::string pendingDBCPath{};
  std::string dbcStatus{};
  bool hasPendingDBCPath = false;
  bool dbcDialogActive = false;

  void draw(GUI& gui);
  void drawDBCSelector(GUI& gui);
};
