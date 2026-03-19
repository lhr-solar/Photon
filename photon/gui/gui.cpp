#include "gui.hpp"

#include <cfloat>

#include "imgui.h"

namespace {
ImGuiKey sdlScancodeToImGuiKey(SDL_Scancode scancode) {
    switch (scancode) {
        case SDL_SCANCODE_TAB: return ImGuiKey_Tab;
        case SDL_SCANCODE_LEFT: return ImGuiKey_LeftArrow;
        case SDL_SCANCODE_RIGHT: return ImGuiKey_RightArrow;
        case SDL_SCANCODE_UP: return ImGuiKey_UpArrow;
        case SDL_SCANCODE_DOWN: return ImGuiKey_DownArrow;
        case SDL_SCANCODE_PAGEUP: return ImGuiKey_PageUp;
        case SDL_SCANCODE_PAGEDOWN: return ImGuiKey_PageDown;
        case SDL_SCANCODE_HOME: return ImGuiKey_Home;
        case SDL_SCANCODE_END: return ImGuiKey_End;
        case SDL_SCANCODE_INSERT: return ImGuiKey_Insert;
        case SDL_SCANCODE_DELETE: return ImGuiKey_Delete;
        case SDL_SCANCODE_BACKSPACE: return ImGuiKey_Backspace;
        case SDL_SCANCODE_SPACE: return ImGuiKey_Space;
        case SDL_SCANCODE_RETURN: return ImGuiKey_Enter;
        case SDL_SCANCODE_ESCAPE: return ImGuiKey_Escape;
        case SDL_SCANCODE_APOSTROPHE: return ImGuiKey_Apostrophe;
        case SDL_SCANCODE_COMMA: return ImGuiKey_Comma;
        case SDL_SCANCODE_MINUS: return ImGuiKey_Minus;
        case SDL_SCANCODE_PERIOD: return ImGuiKey_Period;
        case SDL_SCANCODE_SLASH: return ImGuiKey_Slash;
        case SDL_SCANCODE_SEMICOLON: return ImGuiKey_Semicolon;
        case SDL_SCANCODE_EQUALS: return ImGuiKey_Equal;
        case SDL_SCANCODE_LEFTBRACKET: return ImGuiKey_LeftBracket;
        case SDL_SCANCODE_BACKSLASH: return ImGuiKey_Backslash;
        case SDL_SCANCODE_RIGHTBRACKET: return ImGuiKey_RightBracket;
        case SDL_SCANCODE_GRAVE: return ImGuiKey_GraveAccent;
        case SDL_SCANCODE_CAPSLOCK: return ImGuiKey_CapsLock;
        case SDL_SCANCODE_SCROLLLOCK: return ImGuiKey_ScrollLock;
        case SDL_SCANCODE_NUMLOCKCLEAR: return ImGuiKey_NumLock;
        case SDL_SCANCODE_PRINTSCREEN: return ImGuiKey_PrintScreen;
        case SDL_SCANCODE_PAUSE: return ImGuiKey_Pause;
        case SDL_SCANCODE_KP_0: return ImGuiKey_Keypad0;
        case SDL_SCANCODE_KP_1: return ImGuiKey_Keypad1;
        case SDL_SCANCODE_KP_2: return ImGuiKey_Keypad2;
        case SDL_SCANCODE_KP_3: return ImGuiKey_Keypad3;
        case SDL_SCANCODE_KP_4: return ImGuiKey_Keypad4;
        case SDL_SCANCODE_KP_5: return ImGuiKey_Keypad5;
        case SDL_SCANCODE_KP_6: return ImGuiKey_Keypad6;
        case SDL_SCANCODE_KP_7: return ImGuiKey_Keypad7;
        case SDL_SCANCODE_KP_8: return ImGuiKey_Keypad8;
        case SDL_SCANCODE_KP_9: return ImGuiKey_Keypad9;
        case SDL_SCANCODE_KP_PERIOD: return ImGuiKey_KeypadDecimal;
        case SDL_SCANCODE_KP_DIVIDE: return ImGuiKey_KeypadDivide;
        case SDL_SCANCODE_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
        case SDL_SCANCODE_KP_MINUS: return ImGuiKey_KeypadSubtract;
        case SDL_SCANCODE_KP_PLUS: return ImGuiKey_KeypadAdd;
        case SDL_SCANCODE_KP_ENTER: return ImGuiKey_KeypadEnter;
        case SDL_SCANCODE_KP_EQUALS: return ImGuiKey_KeypadEqual;
        case SDL_SCANCODE_LCTRL: return ImGuiKey_LeftCtrl;
        case SDL_SCANCODE_LSHIFT: return ImGuiKey_LeftShift;
        case SDL_SCANCODE_LALT: return ImGuiKey_LeftAlt;
        case SDL_SCANCODE_LGUI: return ImGuiKey_LeftSuper;
        case SDL_SCANCODE_RCTRL: return ImGuiKey_RightCtrl;
        case SDL_SCANCODE_RSHIFT: return ImGuiKey_RightShift;
        case SDL_SCANCODE_RALT: return ImGuiKey_RightAlt;
        case SDL_SCANCODE_RGUI: return ImGuiKey_RightSuper;
        case SDL_SCANCODE_MENU: return ImGuiKey_Menu;
        case SDL_SCANCODE_0: return ImGuiKey_0;
        case SDL_SCANCODE_1: return ImGuiKey_1;
        case SDL_SCANCODE_2: return ImGuiKey_2;
        case SDL_SCANCODE_3: return ImGuiKey_3;
        case SDL_SCANCODE_4: return ImGuiKey_4;
        case SDL_SCANCODE_5: return ImGuiKey_5;
        case SDL_SCANCODE_6: return ImGuiKey_6;
        case SDL_SCANCODE_7: return ImGuiKey_7;
        case SDL_SCANCODE_8: return ImGuiKey_8;
        case SDL_SCANCODE_9: return ImGuiKey_9;
        case SDL_SCANCODE_A: return ImGuiKey_A;
        case SDL_SCANCODE_B: return ImGuiKey_B;
        case SDL_SCANCODE_C: return ImGuiKey_C;
        case SDL_SCANCODE_D: return ImGuiKey_D;
        case SDL_SCANCODE_E: return ImGuiKey_E;
        case SDL_SCANCODE_F: return ImGuiKey_F;
        case SDL_SCANCODE_G: return ImGuiKey_G;
        case SDL_SCANCODE_H: return ImGuiKey_H;
        case SDL_SCANCODE_I: return ImGuiKey_I;
        case SDL_SCANCODE_J: return ImGuiKey_J;
        case SDL_SCANCODE_K: return ImGuiKey_K;
        case SDL_SCANCODE_L: return ImGuiKey_L;
        case SDL_SCANCODE_M: return ImGuiKey_M;
        case SDL_SCANCODE_N: return ImGuiKey_N;
        case SDL_SCANCODE_O: return ImGuiKey_O;
        case SDL_SCANCODE_P: return ImGuiKey_P;
        case SDL_SCANCODE_Q: return ImGuiKey_Q;
        case SDL_SCANCODE_R: return ImGuiKey_R;
        case SDL_SCANCODE_S: return ImGuiKey_S;
        case SDL_SCANCODE_T: return ImGuiKey_T;
        case SDL_SCANCODE_U: return ImGuiKey_U;
        case SDL_SCANCODE_V: return ImGuiKey_V;
        case SDL_SCANCODE_W: return ImGuiKey_W;
        case SDL_SCANCODE_X: return ImGuiKey_X;
        case SDL_SCANCODE_Y: return ImGuiKey_Y;
        case SDL_SCANCODE_Z: return ImGuiKey_Z;
        case SDL_SCANCODE_F1: return ImGuiKey_F1;
        case SDL_SCANCODE_F2: return ImGuiKey_F2;
        case SDL_SCANCODE_F3: return ImGuiKey_F3;
        case SDL_SCANCODE_F4: return ImGuiKey_F4;
        case SDL_SCANCODE_F5: return ImGuiKey_F5;
        case SDL_SCANCODE_F6: return ImGuiKey_F6;
        case SDL_SCANCODE_F7: return ImGuiKey_F7;
        case SDL_SCANCODE_F8: return ImGuiKey_F8;
        case SDL_SCANCODE_F9: return ImGuiKey_F9;
        case SDL_SCANCODE_F10: return ImGuiKey_F10;
        case SDL_SCANCODE_F11: return ImGuiKey_F11;
        case SDL_SCANCODE_F12: return ImGuiKey_F12;
        default: return ImGuiKey_None;
    }
}

void addModifierEvents(ImGuiIO& io, SDL_Keymod mod) {
    io.AddKeyEvent(ImGuiMod_Ctrl, (mod & SDL_KMOD_CTRL) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (mod & SDL_KMOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (mod & SDL_KMOD_ALT) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (mod & SDL_KMOD_GUI) != 0);
}

int sdlMouseButtonToImGuiButton(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return 0;
        case SDL_BUTTON_RIGHT: return 1;
        case SDL_BUTTON_MIDDLE: return 2;
        case SDL_BUTTON_X1: return 3;
        case SDL_BUTTON_X2: return 4;
        default: return -1;
    }
}
}

void GUI::processEvents(SDL_Event* events){
    if (events == nullptr) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    switch (events->type) {
        case SDL_EVENT_MOUSE_MOTION:
            io.AddMousePosEvent(events->motion.x, events->motion.y);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const int button = sdlMouseButtonToImGuiButton(events->button.button);
            if (button >= 0) {
                io.AddMousePosEvent(events->button.x, events->button.y);
                io.AddMouseButtonEvent(button, events->button.down);
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            float wheelX = events->wheel.x;
            float wheelY = events->wheel.y;
            if (events->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                wheelX = -wheelX;
                wheelY = -wheelY;
            }
            io.AddMouseWheelEvent(wheelX, wheelY);
            break;
        }
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            io.AddFocusEvent(true);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            io.AddFocusEvent(false);
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            addModifierEvents(io, events->key.mod);
            const ImGuiKey key = sdlScancodeToImGuiKey(events->key.scancode);
            if (key != ImGuiKey_None) {
                io.AddKeyEvent(key, events->key.down);
                io.SetKeyEventNativeData(key, events->key.key, events->key.scancode, events->key.scancode);
            }
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            if (events->text.text != nullptr) {
                io.AddInputCharactersUTF8(events->text.text);
            }
            break;
        default:
            break;
    }
}
