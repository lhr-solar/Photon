#pragma once
struct GUI;
struct TitleBar;
struct Canvas;
struct Tabs;

struct Sidebar{
    float width = 360.0f;
    float storedWidth = 360.0f;
    bool showingColors = false;
    bool showingSettings = false;
    bool showingUpdate = false;
    void draw(GUI& gui);
};
