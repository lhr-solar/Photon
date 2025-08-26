/*[μ] the photon graphical user interface*/
#pragma once
#include "xcb/xcb.h"
#include <assert.h>
#include <string>
#include <stdlib.h>
#include <glm/glm.hpp>
#include <array>

class Gui{
private:
    void initxcbConnection();
    void setupWindow();

public:

    xcb_connection_t *connection;
    xcb_screen_t *screen;
    xcb_window_t window;
    xcb_intern_atom_reply_t *atom_wm_delete_window;

    uint32_t width = 1280;
    uint32_t height = 720;

    std::string title = "Photon";
    std::string name = "photon";

    struct{
        bool fullscreen = false;
        bool vsync = false;
        bool transparent = false;
    } settings;

    struct {
        bool displayModels = false;
        bool displayLogos = false;
        bool displayBackground = false;
        bool displayCustomModel = false; // NEW
        bool animateLight = false;
        float lightSpeed = 0.25f;
        float lightTimer = 0.0f;
        std::array<float, 50> frameTimes{};
        float frameTimeMin = 9999.0f, frameTimeMax = 0.0f;
        glm::vec3 modelPosition = glm::vec3(0.0f);
        glm::vec3 modelRotation = glm::vec3(0.0f);
        glm::vec3 modelScale3D = glm::vec3(1.0f);
        float modelScale = 1.0f;
        glm::vec4 effectColor = glm::vec4(1.0f);
        int effectType = 0;
    } renderSettings;

    Gui();
    ~Gui();
    void initWindow();
    std::string getWindowTitle()const;

/* end of gui class */
};
