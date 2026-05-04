#pragma once

struct TitleBar;
struct Sidebar;
struct Tabs;

struct Canvas{
    float width = 0.0f;
    void draw(TitleBar& titleBar, Sidebar& sideBar, Tabs& tabs);
};
