#pragma once

#if defined(_WIN32)
#define KEY_ESCAPE VK_ESCAPE 
#define KEY_F1 VK_F1
#define KEY_F2 VK_F2
#define KEY_F3 VK_F3
#define KEY_F4 VK_F4
#define KEY_F5 VK_F5
#define KEY_W 0x57
#define KEY_A 0x41
#define KEY_S 0x53
#define KEY_D 0x44
#define KEY_P 0x50
#define KEY_SPACE 0x20
#define KEY_KPADD 0x6B
#define KEY_KPSUB 0x6D
#define KEY_B 0x42
#define KEY_F 0x46
#define KEY_L 0x4C
#define KEY_N 0x4E
#define KEY_O 0x4F
#define KEY_T 0x54

#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
#define GAMEPAD_BUTTON_A 0x1000
#define GAMEPAD_BUTTON_B 0x1001
#define GAMEPAD_BUTTON_X 0x1002
#define GAMEPAD_BUTTON_Y 0x1003
#define GAMEPAD_BUTTON_L1 0x1004
#define GAMEPAD_BUTTON_R1 0x1005
#define GAMEPAD_BUTTON_START 0x1006
#define TOUCH_DOUBLE_TAP 0x1100

// for textoverlay example
#define KEY_SPACE 0x3E		// AKEYCODE_SPACE
#define KEY_KPADD 0x9D		// AKEYCODE_NUMPAD_ADD

#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK) || defined(VK_USE_PLATFORM_METAL_EXT))
#if !defined(VK_EXAMPLE_XCODE_GENERATED)
// For iOS and macOS pre-configured Xcode example project: Use character keycodes
// - Use numeric keys as optional alternative to function keys
#define KEY_DELETE 0x7F
#define KEY_ESCAPE 0x1B
#define KEY_F1 0xF704		// NSF1FunctionKey
#define KEY_F2 0xF705		// NSF2FunctionKey
#define KEY_F3 0xF706		// NSF3FunctionKey
#define KEY_F4 0xF707		// NSF4FunctionKey
#define KEY_1 '1'
#define KEY_2 '2'
#define KEY_3 '3'
#define KEY_4 '4'
#define KEY_W 'w'
#define KEY_A 'a'
#define KEY_S 's'
#define KEY_D 'd'
#define KEY_P 'p'
#define KEY_SPACE ' '
#define KEY_KPADD '+'
#define KEY_KPSUB '-'
#define KEY_B 'b'
#define KEY_F 'f'
#define KEY_L 'l'
#define KEY_N 'n'
#define KEY_O 'o'
#define KEY_Q 'q'
#define KEY_T 't'
#define KEY_Z 'z'

#else // defined(VK_EXAMPLE_XCODE_GENERATED)
// For cross-platform cmake-generated Xcode project: Use ANSI keyboard keycodes
// - Use numeric keys as optional alternative to function keys
// - Use main keyboard plus/minus instead of keypad plus/minus
#include <Carbon/Carbon.h>
#define KEY_DELETE kVK_Delete
#define KEY_ESCAPE kVK_Escape
#define KEY_F1 kVK_F1
#define KEY_F2 kVK_F2
#define KEY_F3 kVK_F3
#define KEY_F4 kVK_F4
#define KEY_1 kVK_ANSI_1
#define KEY_2 kVK_ANSI_2
#define KEY_3 kVK_ANSI_3
#define KEY_4 kVK_ANSI_4
#define KEY_W kVK_ANSI_W
#define KEY_A kVK_ANSI_A
#define KEY_S kVK_ANSI_S
#define KEY_D kVK_ANSI_D
#define KEY_P kVK_ANSI_P
#define KEY_SPACE kVK_Space
#define KEY_KPADD kVK_ANSI_Equal
#define KEY_KPSUB kVK_ANSI_Minus
#define KEY_B kVK_ANSI_B
#define KEY_F kVK_ANSI_F
#define KEY_L kVK_ANSI_L
#define KEY_N kVK_ANSI_N
#define KEY_O kVK_ANSI_O
#define KEY_Q kVK_ANSI_Q
#define KEY_T kVK_ANSI_T
#define KEY_Z kVK_ANSI_Z
#endif

#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
#define KEY_ESCAPE DIKS_ESCAPE
#define KEY_F1 DIKS_F1
#define KEY_F2 DIKS_F2
#define KEY_F3 DIKS_F3
#define KEY_F4 DIKS_F4
#define KEY_W DIKS_SMALL_W
#define KEY_A DIKS_SMALL_A
#define KEY_S DIKS_SMALL_S
#define KEY_D DIKS_SMALL_D
#define KEY_P DIKS_SMALL_P
#define KEY_SPACE DIKS_SPACE
#define KEY_KPADD DIKS_PLUS_SIGN
#define KEY_KPSUB DIKS_MINUS_SIGN
#define KEY_B DIKS_SMALL_B
#define KEY_F DIKS_SMALL_F
#define KEY_L DIKS_SMALL_L
#define KEY_N DIKS_SMALL_N
#define KEY_O DIKS_SMALL_O
#define KEY_T DIKS_SMALL_T

#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
#include <linux/input.h>

// todo: hack for bloom example
#define KEY_ESCAPE KEY_ESC
#define KEY_KPADD KEY_KPPLUS
#define KEY_KPSUB KEY_KPMINUS

#elif defined(__linux__) || defined(__FreeBSD__)
  // — Control keys —
  #define KEY_ESCAPE     0x09  // Esc (scan 0x01 + 8)
  #define KEY_TAB        0x17  // Tab (scan 0x0F + 8)
  #define KEY_BACKSPACE  0x16  // Backspace (scan 0x0E + 8)
  //#define KEY_ENTER      0x24  // Enter/Return (scan 0x1C + 8)
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

#elif defined(VK_USE_PLATFORM_SCREEN_QNX)
#include <sys/keycodes.h>

#define KEY_ESCAPE KEYCODE_ESCAPE
#define KEY_F1     KEYCODE_F1
#define KEY_F2     KEYCODE_F2
#define KEY_F3     KEYCODE_F3
#define KEY_F4     KEYCODE_F4
#define KEY_W      KEYCODE_W
#define KEY_A      KEYCODE_A
#define KEY_S      KEYCODE_S
#define KEY_D      KEYCODE_D
#define KEY_P      KEYCODE_P
#define KEY_SPACE  KEYCODE_SPACE
#define KEY_KPADD  KEYCODE_KP_PLUS
#define KEY_KPSUB  KEYCODE_KP_MINUS
#define KEY_B      KEYCODE_B
#define KEY_F      KEYCODE_F
#define KEY_L      KEYCODE_L
#define KEY_N      KEYCODE_N
#define KEY_O      KEYCODE_O
#define KEY_T      KEYCODE_T

#endif
