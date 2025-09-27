#include "inputs.hpp"

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
