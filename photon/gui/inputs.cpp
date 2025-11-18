#include "inputs.hpp"
#ifdef XCB
#include <xcb/xcb.h>
#endif

#ifdef WIN
#include <windowsx.h>
#endif

#include "../engine/include.hpp"

void Inputs::handleMouseMove(int32_t x, int32_t y){
    mouseState.position = glm::vec2(static_cast<float>(x), static_cast<float>(y));
    if (ImGui::GetCurrentContext()) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
    }
}

ImGuiKey Inputs::translateKey(uint32_t key){
#ifdef XCB
    switch (key) {
        // — Printable characters —
        case KEY_SPACE:         return ImGuiKey_Space;
        case KEY_A:             return ImGuiKey_A;
        case KEY_B:             return ImGuiKey_B;
        case KEY_C:             return ImGuiKey_C;
        case KEY_D:             return ImGuiKey_D;
        case KEY_E:             return ImGuiKey_E;
        case KEY_F:             return ImGuiKey_F;
        case KEY_G:             return ImGuiKey_G;
        case KEY_H:             return ImGuiKey_H;
        case KEY_I:             return ImGuiKey_I;
        case KEY_J:             return ImGuiKey_J;
        case KEY_K:             return ImGuiKey_K;
        case KEY_L:             return ImGuiKey_L;
        case KEY_M:             return ImGuiKey_M;
        case KEY_N:             return ImGuiKey_N;
        case KEY_O:             return ImGuiKey_O;
        case KEY_P:             return ImGuiKey_P;
        case KEY_Q:             return ImGuiKey_Q;
        case KEY_R:             return ImGuiKey_R;
        case KEY_S:             return ImGuiKey_S;
        case KEY_T:             return ImGuiKey_T;
        case KEY_U:             return ImGuiKey_U;
        case KEY_V:             return ImGuiKey_V;
        case KEY_W:             return ImGuiKey_W;
        case KEY_X:             return ImGuiKey_X;
        case KEY_Y:             return ImGuiKey_Y;
        case KEY_Z:             return ImGuiKey_Z;

        // — Control keys —
        case KEY_TAB:           return ImGuiKey_Tab;
        case KEY_ENTER:         return ImGuiKey_Enter;
        case KEY_BACKSPACE:     return ImGuiKey_Backspace;
        case KEY_ESCAPE:        return ImGuiKey_Escape;
        case KEY_LEFT_CTRL:     return ImGuiKey_LeftCtrl;
        case KEY_RIGHT_CTRL:    return ImGuiKey_RightCtrl;
        case KEY_LEFT_SHIFT:    return ImGuiKey_LeftShift;
        case KEY_RIGHT_SHIFT:   return ImGuiKey_RightShift;
        case KEY_LEFT_ALT:      return ImGuiKey_LeftAlt;
        case KEY_RIGHT_ALT:     return ImGuiKey_RightAlt;
        case KEY_LEFT_SUPER:    return ImGuiKey_LeftSuper;
        case KEY_RIGHT_SUPER:   return ImGuiKey_RightSuper;

        // — Keypad —
        case KEY_1:             return ImGuiKey_1;
        case KEY_2:             return ImGuiKey_2;
        case KEY_3:             return ImGuiKey_3;
        case KEY_4:             return ImGuiKey_4;
        case KEY_5:             return ImGuiKey_5;
        case KEY_6:             return ImGuiKey_6;
        case KEY_7:             return ImGuiKey_7;
        case KEY_8:             return ImGuiKey_8;
        case KEY_9:             return ImGuiKey_9;
        case KEY_0:             return ImGuiKey_0;

        case KEY_SLASH:         return ImGuiKey_Slash;
        case KEY_PERIOD:        return ImGuiKey_Period;

        default:
            return ImGuiKey_None;
    }
#endif
    return ImGuiKey_None;
}

#ifdef XCB
void Inputs::handleXcbEvent(const xcb_generic_event_t *event, bool &quitFlag, xcb_atom_t deleteAtom){
    switch (event->response_type & 0x7f){
    case XCB_CLIENT_MESSAGE:{
        auto *msg = reinterpret_cast<const xcb_client_message_event_t*>(event);
        if (deleteAtom != XCB_ATOM_NONE && msg->data.data32[0] == deleteAtom) {
            quitFlag = true;
        }
        break;
    }
    case XCB_MOTION_NOTIFY:{
        auto *motion = reinterpret_cast<const xcb_motion_notify_event_t*>(event);
        handleMouseMove(static_cast<int32_t>(motion->event_x), static_cast<int32_t>(motion->event_y));
        break;
    }
    case XCB_BUTTON_PRESS:{
        auto *press = reinterpret_cast<const xcb_button_press_event_t*>(event);
        if (press->detail == XCB_BUTTON_INDEX_1){
            ImGui::GetIO().AddMouseButtonEvent(0, true);
            mouseState.buttons.left = true;
        }
        if (press->detail == XCB_BUTTON_INDEX_2){
            ImGui::GetIO().AddMouseButtonEvent(2, true);
            mouseState.buttons.middle = true;
        }
        if (press->detail == XCB_BUTTON_INDEX_3){
            ImGui::GetIO().AddMouseButtonEvent(1, true);
            mouseState.buttons.right = true;
        }
        break;
    }
    case XCB_BUTTON_RELEASE:{
        auto *press = reinterpret_cast<const xcb_button_press_event_t*>(event);
        if (press->detail == XCB_BUTTON_INDEX_1){
            ImGui::GetIO().AddMouseButtonEvent(0, false);
            mouseState.buttons.left = false;
        }
        if (press->detail == XCB_BUTTON_INDEX_2){
            ImGui::GetIO().AddMouseButtonEvent(2, false);
            mouseState.buttons.middle = false;
        }
        if (press->detail == XCB_BUTTON_INDEX_3){
            ImGui::GetIO().AddMouseButtonEvent(1, false);
            mouseState.buttons.right = false;
        }
        break;
    }
    case XCB_KEY_PRESS:{
        const auto *keyEvent = reinterpret_cast<const xcb_key_release_event_t *>(event);
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureKeyboard) {
            ImGuiKey key = translateKey(keyEvent->detail);
            if (key != ImGuiKey_None) { io.AddKeyEvent(key, true); }

            const bool shiftDown  = (keyEvent->state & XCB_MOD_MASK_SHIFT) || key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift;
            const bool ctrlDown   = (keyEvent->state & XCB_MOD_MASK_CONTROL) || key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl;
            const bool altDown    = (keyEvent->state & XCB_MOD_MASK_1) || key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt;
            const bool superDown  = (keyEvent->state & XCB_MOD_MASK_4) || key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper;

            io.AddKeyEvent(ImGuiMod_Shift, shiftDown);
            io.AddKeyEvent(ImGuiMod_Ctrl, ctrlDown);
            io.AddKeyEvent(ImGuiMod_Alt, altDown);
            io.AddKeyEvent(ImGuiMod_Super, superDown);
            uint8_t kc = keyEvent->detail;
            bool shift = keyEvent->state & XCB_MOD_MASK_SHIFT;
            char c = 0;
            switch (kc) {
            case KEY_A: c = shift ? 'A' : 'a'; break;
            case KEY_B: c = shift ? 'B' : 'b'; break;
            case KEY_C: c = shift ? 'C' : 'c'; break;
            case KEY_D: c = shift ? 'D' : 'd'; break;
            case KEY_E: c = shift ? 'E' : 'e'; break;
            case KEY_F: c = shift ? 'F' : 'f'; break;
            case KEY_G: c = shift ? 'G' : 'g'; break;
            case KEY_H: c = shift ? 'H' : 'h'; break;
            case KEY_I: c = shift ? 'I' : 'i'; break;
            case KEY_J: c = shift ? 'J' : 'j'; break;
            case KEY_K: c = shift ? 'K' : 'k'; break;
            case KEY_L: c = shift ? 'L' : 'l'; break;
            case KEY_M: c = shift ? 'M' : 'm'; break;
            case KEY_N: c = shift ? 'N' : 'n'; break;
            case KEY_O: c = shift ? 'O' : 'o'; break;
            case KEY_P: c = shift ? 'P' : 'p'; break;
            case KEY_Q: c = shift ? 'Q' : 'q'; break;
            case KEY_R: c = shift ? 'R' : 'r'; break;
            case KEY_S: c = shift ? 'S' : 's'; break;
            case KEY_T: c = shift ? 'T' : 't'; break;
            case KEY_U: c = shift ? 'U' : 'u'; break;
            case KEY_V: c = shift ? 'V' : 'v'; break;
            case KEY_W: c = shift ? 'W' : 'w'; break;
            case KEY_X: c = shift ? 'X' : 'x'; break;
            case KEY_Y: c = shift ? 'Y' : 'y'; break;
            case KEY_Z: c = shift ? 'Z' : 'z'; break;
            case KEY_1: c = shift ? '!' : '1'; break;
            case KEY_2: c = shift ? '@' : '2'; break;
            case KEY_3: c = shift ? '#' : '3'; break;
            case KEY_4: c = shift ? '$' : '4'; break;
            case KEY_5: c = shift ? '%' : '5'; break;
            case KEY_6: c = shift ? '^' : '6'; break;
            case KEY_7: c = shift ? '&' : '7'; break;
            case KEY_8: c = shift ? '*' : '8'; break;
            case KEY_9: c = shift ? '(' : '9'; break;
            case KEY_0: c = shift ? ')' : '0'; break;
            case KEY_SLASH:       c = shift ? '?' : '/'; break;
            case KEY_PERIOD:      c = shift ? '>' : '.'; break;
            case KEY_SPACE:       c = ' ';  break;
            case KEY_ENTER:       c = '\n'; break;
            case KEY_TAB:         c = '\t'; break;
            }
            if (c) { io.AddInputCharacter(c); }
        }
        break;
    }
    case XCB_KEY_RELEASE:{
        const auto *keyEvent = reinterpret_cast<const xcb_key_release_event_t *>(event);
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureKeyboard) {
            ImGuiKey key = translateKey(keyEvent->detail);
            if (key != ImGuiKey_None) { io.AddKeyEvent(key, false); }

            bool shiftDown  = (keyEvent->state & XCB_MOD_MASK_SHIFT);
            bool ctrlDown   = (keyEvent->state & XCB_MOD_MASK_CONTROL);
            bool altDown    = (keyEvent->state & XCB_MOD_MASK_1);
            bool superDown  = (keyEvent->state & XCB_MOD_MASK_4);

            if (key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift) shiftDown = false;
            if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl)   ctrlDown = false;
            if (key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt)     altDown = false;
            if (key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper) superDown = false;

            io.AddKeyEvent(ImGuiMod_Shift, shiftDown);
            io.AddKeyEvent(ImGuiMod_Ctrl, ctrlDown);
            io.AddKeyEvent(ImGuiMod_Alt, altDown);
            io.AddKeyEvent(ImGuiMod_Super, superDown);
        }
        break;
    }
    case XCB_DESTROY_NOTIFY:
        quitFlag = true;
        break;
    default:
        break;
    }
}
#endif

#if defined(_WIN32)
ImGuiKey Inputs::translateWin32Key(uint32_t key){
    if (key >= '0' && key <= '9') {
        return static_cast<ImGuiKey>(ImGuiKey_0 + (key - '0'));
    }
    if (key >= 'A' && key <= 'Z') {
        return static_cast<ImGuiKey>(ImGuiKey_A + (key - 'A'));
    }

    switch (key) {
    case VK_ESCAPE:    return ImGuiKey_Escape;
    case VK_RETURN:    return ImGuiKey_Enter;
    case VK_TAB:       return ImGuiKey_Tab;
    case VK_BACK:      return ImGuiKey_Backspace;
    case VK_SPACE:     return ImGuiKey_Space;
    case VK_DELETE:    return ImGuiKey_Delete;
    case VK_INSERT:    return ImGuiKey_Insert;
    case VK_HOME:      return ImGuiKey_Home;
    case VK_END:       return ImGuiKey_End;
    case VK_PRIOR:     return ImGuiKey_PageUp;
    case VK_NEXT:      return ImGuiKey_PageDown;
    case VK_UP:        return ImGuiKey_UpArrow;
    case VK_DOWN:      return ImGuiKey_DownArrow;
    case VK_LEFT:      return ImGuiKey_LeftArrow;
    case VK_RIGHT:     return ImGuiKey_RightArrow;
    case VK_LSHIFT:    return ImGuiKey_LeftShift;
    case VK_RSHIFT:    return ImGuiKey_RightShift;
    case VK_SHIFT:     return (GetKeyState(VK_RSHIFT) & 0x8000) ? ImGuiKey_RightShift : ImGuiKey_LeftShift;
    case VK_LCONTROL:  return ImGuiKey_LeftCtrl;
    case VK_RCONTROL:  return ImGuiKey_RightCtrl;
    case VK_CONTROL:   return (GetKeyState(VK_RCONTROL) & 0x8000) ? ImGuiKey_RightCtrl : ImGuiKey_LeftCtrl;
    case VK_LMENU:     return ImGuiKey_LeftAlt;
    case VK_RMENU:     return ImGuiKey_RightAlt;
    case VK_MENU:      return (GetKeyState(VK_RMENU) & 0x8000) ? ImGuiKey_RightAlt : ImGuiKey_LeftAlt;
    case VK_LWIN:      return ImGuiKey_LeftSuper;
    case VK_RWIN:      return ImGuiKey_RightSuper;
    case VK_OEM_PLUS:  return ImGuiKey_Equal;
    case VK_OEM_MINUS: return ImGuiKey_Minus;
    case VK_OEM_COMMA: return ImGuiKey_Comma;
    case VK_OEM_PERIOD:return ImGuiKey_Period;
    case VK_OEM_1:     return ImGuiKey_Semicolon;
    case VK_OEM_2:     return ImGuiKey_Slash;
    case VK_OEM_3:     return ImGuiKey_GraveAccent;
    case VK_OEM_4:     return ImGuiKey_LeftBracket;
    case VK_OEM_5:     return ImGuiKey_Backslash;
    case VK_OEM_6:     return ImGuiKey_RightBracket;
    case VK_OEM_7:     return ImGuiKey_Apostrophe;
    case VK_CAPITAL:  return ImGuiKey_CapsLock;
    case VK_NUMLOCK:  return ImGuiKey_NumLock;
    case VK_SCROLL:   return ImGuiKey_ScrollLock;
    case VK_APPS:     return ImGuiKey_Menu;
    }

    if (key >= VK_F1 && key <= VK_F24) {
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (key - VK_F1));
    }

    return ImGuiKey_None;
}
#endif

#ifdef WIN
LRESULT Inputs::handleWin32Message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool &quitFlag, uint32_t &destWidth, uint32_t &destHeight){
    ImGuiContext* context = ImGui::GetCurrentContext();
    ImGuiIO* io = context ? &ImGui::GetIO() : nullptr;

    switch (uMsg) {
    case WM_CLOSE:
        quitFlag = true;
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        quitFlag = true;
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            destWidth = static_cast<uint32_t>(LOWORD(lParam));
            destHeight = static_cast<uint32_t>(HIWORD(lParam));
        }
        return 0;
    case WM_MOUSEMOVE: {
        int32_t x = GET_X_LPARAM(lParam);
        int32_t y = GET_Y_LPARAM(lParam);
        handleMouseMove(x, y);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP: {
        const bool pressed = (uMsg == WM_LBUTTONDOWN) || (uMsg == WM_RBUTTONDOWN) || (uMsg == WM_MBUTTONDOWN);
        const int buttonIndex = (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP) ? 0 :
                                (uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONUP) ? 1 : 2;

        if (pressed) {
            SetCapture(hWnd);
        } else {
            ReleaseCapture();
        }

        if (buttonIndex == 0) {
            mouseState.buttons.left = pressed;
        } else if (buttonIndex == 1) {
            mouseState.buttons.right = pressed;
        } else {
            mouseState.buttons.middle = pressed;
        }

        if (io) {
            io->AddMouseButtonEvent(buttonIndex, pressed);
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (io) {
            const float wheelDelta = static_cast<SHORT>(HIWORD(wParam)) / static_cast<float>(WHEEL_DELTA);
            io->AddMouseWheelEvent(0.0f, wheelDelta);
        }
        return 0;
    case WM_MOUSEHWHEEL:
        if (io) {
            const float wheelDelta = static_cast<SHORT>(HIWORD(wParam)) / static_cast<float>(WHEEL_DELTA);
            io->AddMouseWheelEvent(wheelDelta, 0.0f);
        }
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        const bool pressed = (uMsg == WM_KEYDOWN) || (uMsg == WM_SYSKEYDOWN);
        if (io) {
            ImGuiKey key = translateWin32Key(static_cast<uint32_t>(wParam));
            if (key != ImGuiKey_None) {
                io->AddKeyEvent(key, pressed);
            }

            const bool leftShiftDown  = (GetKeyState(VK_LSHIFT) & 0x8000) != 0;
            const bool rightShiftDown = (GetKeyState(VK_RSHIFT) & 0x8000) != 0;
            const bool leftCtrlDown   = (GetKeyState(VK_LCONTROL) & 0x8000) != 0;
            const bool rightCtrlDown  = (GetKeyState(VK_RCONTROL) & 0x8000) != 0;
            const bool leftAltDown    = (GetKeyState(VK_LMENU) & 0x8000) != 0;
            const bool rightAltDown   = (GetKeyState(VK_RMENU) & 0x8000) != 0;
            const bool leftSuperDown  = (GetKeyState(VK_LWIN) & 0x8000) != 0;
            const bool rightSuperDown = (GetKeyState(VK_RWIN) & 0x8000) != 0;

            io->AddKeyEvent(ImGuiKey_LeftShift, leftShiftDown);
            io->AddKeyEvent(ImGuiKey_RightShift, rightShiftDown);
            io->AddKeyEvent(ImGuiKey_LeftCtrl, leftCtrlDown);
            io->AddKeyEvent(ImGuiKey_RightCtrl, rightCtrlDown);
            io->AddKeyEvent(ImGuiKey_LeftAlt, leftAltDown);
            io->AddKeyEvent(ImGuiKey_RightAlt, rightAltDown);
            io->AddKeyEvent(ImGuiKey_LeftSuper, leftSuperDown);
            io->AddKeyEvent(ImGuiKey_RightSuper, rightSuperDown);

            io->AddKeyEvent(ImGuiMod_Shift, leftShiftDown || rightShiftDown);
            io->AddKeyEvent(ImGuiMod_Ctrl, leftCtrlDown || rightCtrlDown);
            io->AddKeyEvent(ImGuiMod_Alt, leftAltDown || rightAltDown);
            io->AddKeyEvent(ImGuiMod_Super, leftSuperDown || rightSuperDown);
        }
        return 0;
    }
    case WM_CHAR:
        if (io && wParam > 0 && wParam < 0x10000) {
            io->AddInputCharacterUTF16(static_cast<unsigned short>(wParam));
        }
        return 0;
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}
#endif
