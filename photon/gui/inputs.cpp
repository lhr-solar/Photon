#include "inputs.hpp"

#include "../engine/include.hpp"

#ifdef WIN
#include <windows.h>
#endif

void Inputs::handleMouseMove(int32_t x, int32_t y){
    int32_t dx = (int32_t)mouseState.position.x - x;
	int32_t dy = (int32_t)mouseState.position.y - y;
    bool handled = false;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent((float)x, (float)y);
    mouseState.position = glm::vec2((float)x, (float)y);
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
#ifdef WIN
    switch (key) {
    case VK_SPACE:         return ImGuiKey_Space;
    case 'A':              return ImGuiKey_A;
    case 'B':              return ImGuiKey_B;
    case 'C':              return ImGuiKey_C;
    case 'D':              return ImGuiKey_D;
    case 'E':              return ImGuiKey_E;
    case 'F':              return ImGuiKey_F;
    case 'G':              return ImGuiKey_G;
    case 'H':              return ImGuiKey_H;
    case 'I':              return ImGuiKey_I;
    case 'J':              return ImGuiKey_J;
    case 'K':              return ImGuiKey_K;
    case 'L':              return ImGuiKey_L;
    case 'M':              return ImGuiKey_M;
    case 'N':              return ImGuiKey_N;
    case 'O':              return ImGuiKey_O;
    case 'P':              return ImGuiKey_P;
    case 'Q':              return ImGuiKey_Q;
    case 'R':              return ImGuiKey_R;
    case 'S':              return ImGuiKey_S;
    case 'T':              return ImGuiKey_T;
    case 'U':              return ImGuiKey_U;
    case 'V':              return ImGuiKey_V;
    case 'W':              return ImGuiKey_W;
    case 'X':              return ImGuiKey_X;
    case 'Y':              return ImGuiKey_Y;
    case 'Z':              return ImGuiKey_Z;

    case VK_TAB:           return ImGuiKey_Tab;
    case VK_RETURN:        return ImGuiKey_Enter;
    case VK_BACK:          return ImGuiKey_Backspace;
    case VK_ESCAPE:        return ImGuiKey_Escape;

    case '1':              return ImGuiKey_1;
    case '2':              return ImGuiKey_2;
    case '3':              return ImGuiKey_3;
    case '4':              return ImGuiKey_4;
    case '5':              return ImGuiKey_5;
    case '6':              return ImGuiKey_6;
    case '7':              return ImGuiKey_7;
    case '8':              return ImGuiKey_8;
    case '9':              return ImGuiKey_9;
    case '0':              return ImGuiKey_0;

    case VK_OEM_2:         return ImGuiKey_Slash;
    case VK_OEM_PERIOD:    return ImGuiKey_Period;

    case VK_LSHIFT:        return ImGuiKey_LeftShift;
    case VK_RSHIFT:        return ImGuiKey_RightShift;
    case VK_LCONTROL:      return ImGuiKey_LeftCtrl;
    case VK_RCONTROL:      return ImGuiKey_RightCtrl;
    case VK_LMENU:         return ImGuiKey_LeftAlt;
    case VK_RMENU:         return ImGuiKey_RightAlt;
    case VK_LWIN:          return ImGuiKey_LeftSuper;
    case VK_RWIN:          return ImGuiKey_RightSuper;

    default:
        return ImGuiKey_None;
    }
#endif
    return ImGuiKey_None;
}
