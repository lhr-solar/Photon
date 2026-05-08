#pragma once
struct GUI;
struct TitleBar;
struct Canvas;
template <typename Owner>
struct Tabs;
struct ImTextureData;

struct Sidebar{
    float width = 360.0f;
    float storedWidth = 360.0f;
    ImTextureData* backgroundTexture = nullptr;
    void draw(GUI& gui);
};
