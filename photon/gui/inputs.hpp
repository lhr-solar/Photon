#pragma once
#include <stdint.h>
#include "imgui.h"
#include "glm//glm.hpp"

struct Inputs{
    struct {
        struct {
			bool left = false;
			bool right = false;
			bool middle = false;
		} buttons;
		glm::vec2 position;
    } mouseState;

    void handleMouseMove(int32_t x, int32_t y);
    ImGuiKey translateKey(uint32_t key);
#if defined(_WIN32)
    ImGuiKey translateWin32Key(uint32_t key);
#endif
};

// — Control keys —
#define KEY_ESCAPE     0x09  // Esc (scan 0x01 + 8)
#define KEY_TAB        0x17  // Tab (scan 0x0F + 8)
#define KEY_BACKSPACE  0x16  // Backspace (scan 0x0E + 8)
#define KEY_ENTER      0x24  // Enter/Return (scan 0x1C + 8)
#define KEY_SPACE      0x41  // Space (scan 0x39 + 8)

// — Number row —
#define KEY_1          0x0A  // '1' (scan 0x02 + 8)
#define KEY_2          0x0B  // '2' (scan 0x03 + 8)
#define KEY_3          0x0C  // '3' (scan 0x04 + 8)
#define KEY_4          0x0D  // '4' (scan 0x05 + 8)
#define KEY_5          0x0E  // '5' (scan 0x06 + 8)
#define KEY_6          0x0F  // '6' (scan 0x07 + 8)
#define KEY_7          0x10  // '7' (scan 0x08 + 8)
#define KEY_8          0x11  // '8' (scan 0x09 + 8)
#define KEY_9          0x12  // '9' (scan 0x0A + 8)
#define KEY_0          0x13  // '0' (scan 0x0B + 8)

// — QWERTY top row —
#define KEY_Q          0x18  // Q (scan 0x10 + 8)
#define KEY_W          0x19  // W (scan 0x11 + 8)
#define KEY_E          0x1A  // E (scan 0x12 + 8)
#define KEY_R          0x1B  // R (scan 0x13 + 8)
#define KEY_T          0x1C  // T (scan 0x14 + 8)
#define KEY_Y          0x1D  // Y (scan 0x15 + 8)
#define KEY_U          0x1E  // U (scan 0x16 + 8)
#define KEY_I          0x1F  // I (scan 0x17 + 8)
#define KEY_O          0x20  // O (scan 0x18 + 8)
#define KEY_P          0x21  // P (scan 0x19 + 8)

// — QWERTY home row —
#define KEY_A          0x26  // A (scan 0x1E + 8)
#define KEY_S          0x27  // S (scan 0x1F + 8)
#define KEY_D          0x28  // D (scan 0x20 + 8)
#define KEY_F          0x29  // F (scan 0x21 + 8)
#define KEY_G          0x2A  // G (scan 0x22 + 8)
#define KEY_H          0x2B  // H (scan 0x23 + 8)
#define KEY_J          0x2C  // J (scan 0x24 + 8)
#define KEY_K          0x2D  // K (scan 0x25 + 8)
#define KEY_L          0x2E  // L (scan 0x26 + 8)

// — QWERTY bottom row —
#define KEY_Z          0x34  // Z (scan 0x2C + 8)
#define KEY_X          0x35  // X (scan 0x2D + 8)
#define KEY_C          0x36  // C (scan 0x2E + 8)
#define KEY_V          0x37  // V (scan 0x2F + 8)
#define KEY_B          0x38  // B (scan 0x30 + 8)
#define KEY_N          0x39  // N (scan 0x31 + 8)
#define KEY_M          0x3A  // M (scan 0x32 + 8)

// — Function keys (if you still need more than F1–F4) —
#define KEY_F1         0x43  // F1 (scan 0x3B + 8)
#define KEY_F2         0x44  // F2 (scan 0x3C + 8)
#define KEY_F3         0x45  // F3 (scan 0x3D + 8)
#define KEY_F4         0x46  // F4 (scan 0x3E + 8)
#define KEY_SLASH      0x3D  // '/' key
#define KEY_PERIOD     0x3C  // '.' key
