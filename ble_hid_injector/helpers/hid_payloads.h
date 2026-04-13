#pragma once

#include <stdint.h>
#include <ble_profile/extra_profiles/hid_profile.h>

// HID USB keycodes
#define HID_KEY_NONE  0x00
#define HID_KEY_A     0x04
#define HID_KEY_B     0x05
#define HID_KEY_C     0x06
#define HID_KEY_D     0x07
#define HID_KEY_E     0x08
#define HID_KEY_F     0x09
#define HID_KEY_G     0x0A
#define HID_KEY_H     0x0B
#define HID_KEY_I     0x0C
#define HID_KEY_J     0x0D
#define HID_KEY_K     0x0E
#define HID_KEY_L     0x0F
#define HID_KEY_M     0x10
#define HID_KEY_N     0x11
#define HID_KEY_O     0x12
#define HID_KEY_P     0x13
#define HID_KEY_Q     0x14
#define HID_KEY_R     0x15
#define HID_KEY_S     0x16
#define HID_KEY_T     0x17
#define HID_KEY_U     0x18
#define HID_KEY_V     0x19
#define HID_KEY_W     0x1A
#define HID_KEY_X     0x1B
#define HID_KEY_Y     0x1C
#define HID_KEY_Z     0x1D
#define HID_KEY_1     0x1E
#define HID_KEY_2     0x1F
#define HID_KEY_3     0x20
#define HID_KEY_4     0x21
#define HID_KEY_5     0x22
#define HID_KEY_ENTER  0x28
#define HID_KEY_ESC    0x29
#define HID_KEY_TAB    0x2B
#define HID_KEY_SPACE  0x2C
#define HID_KEY_MINUS  0x2D
#define HID_KEY_EQUAL  0x2E
#define HID_KEY_SLASH  0x38
#define HID_KEY_PERIOD 0x37
#define HID_KEY_F5     0x3E
#define HID_KEY_F11    0x44

// Modifier keys
#define HID_MOD_LCTRL  (1 << 0)
#define HID_MOD_LSHIFT (1 << 1)
#define HID_MOD_LALT   (1 << 2)
#define HID_MOD_LGUI   (1 << 3) // Windows/Command key
#define HID_MOD_RCTRL  (1 << 4)

// Keystroke: modifier + keycode
typedef struct {
    uint8_t mod;
    uint8_t key;
    uint16_t delay_ms; // delay after this key (0 = default 50ms)
} HidKeystroke;

// A payload is a sequence of keystrokes
typedef struct {
    const char* name;
    const char* description;
    const HidKeystroke* keys;
    uint8_t key_count;
} HidPayload;

// Payload: Hello World (proof of concept)
static const HidKeystroke payload_hello[] = {
    {HID_MOD_LSHIFT, HID_KEY_H, 0},
    {0, HID_KEY_E, 0},
    {0, HID_KEY_L, 0},
    {0, HID_KEY_L, 0},
    {0, HID_KEY_O, 0},
    {0, HID_KEY_SPACE, 0},
    {HID_MOD_LSHIFT, HID_KEY_W, 0},
    {0, HID_KEY_O, 0},
    {0, HID_KEY_R, 0},
    {0, HID_KEY_L, 0},
    {0, HID_KEY_D, 0},
    {0, HID_KEY_ENTER, 0},
};

// Payload: Windows Run dialog -> calc
static const HidKeystroke payload_win_calc[] = {
    {HID_MOD_LGUI, HID_KEY_R, 500},       // Win+R
    {0, HID_KEY_C, 0},                     // calc
    {0, HID_KEY_A, 0},
    {0, HID_KEY_L, 0},
    {0, HID_KEY_C, 0},
    {0, HID_KEY_ENTER, 0},                 // Enter
};

// Payload: macOS Spotlight -> Terminal
static const HidKeystroke payload_mac_terminal[] = {
    {HID_MOD_LGUI, HID_KEY_SPACE, 500},   // Cmd+Space (Spotlight)
    {0, HID_KEY_T, 100},                   // terminal
    {0, HID_KEY_E, 0},
    {0, HID_KEY_R, 0},
    {0, HID_KEY_M, 0},
    {0, HID_KEY_I, 0},
    {0, HID_KEY_N, 0},
    {0, HID_KEY_A, 0},
    {0, HID_KEY_L, 0},
    {0, HID_KEY_ENTER, 500},               // Enter (open Terminal)
};

// Payload: Linux Ctrl+Alt+T (open terminal)
static const HidKeystroke payload_linux_term[] = {
    {HID_MOD_LCTRL | HID_MOD_LALT, HID_KEY_T, 500}, // Ctrl+Alt+T
};

// Payload: Android (just type text — no privileged action)
static const HidKeystroke payload_android_test[] = {
    {0, HID_KEY_H, 0},
    {0, HID_KEY_I, 0},
    {0, HID_KEY_SPACE, 0},
    {0, HID_KEY_F, 0},
    {0, HID_KEY_R, 0},
    {0, HID_KEY_O, 0},
    {0, HID_KEY_M, 0},
    {0, HID_KEY_SPACE, 0},
    {0, HID_KEY_F, 0},
    {0, HID_KEY_L, 0},
    {0, HID_KEY_I, 0},
    {0, HID_KEY_P, 0},
    {0, HID_KEY_P, 0},
    {0, HID_KEY_E, 0},
    {0, HID_KEY_R, 0},
};

// Payload: Lock screen (Win+L / Cmd+Ctrl+Q)
static const HidKeystroke payload_lock_win[] = {
    {HID_MOD_LGUI, HID_KEY_L, 0},         // Win+L
};

#define PAYLOAD_COUNT 6

static const HidPayload hid_payloads[PAYLOAD_COUNT] = {
    {"Hello World",     "Type 'Hello World'",             payload_hello,        sizeof(payload_hello) / sizeof(HidKeystroke)},
    {"Win: Calculator", "Win+R -> calc",                  payload_win_calc,     sizeof(payload_win_calc) / sizeof(HidKeystroke)},
    {"Mac: Terminal",   "Cmd+Space -> terminal",          payload_mac_terminal, sizeof(payload_mac_terminal) / sizeof(HidKeystroke)},
    {"Linux: Terminal", "Ctrl+Alt+T",                     payload_linux_term,   sizeof(payload_linux_term) / sizeof(HidKeystroke)},
    {"Android: Text",   "Type test text",                 payload_android_test, sizeof(payload_android_test) / sizeof(HidKeystroke)},
    {"Win: Lock",       "Win+L lock screen",              payload_lock_win,     sizeof(payload_lock_win) / sizeof(HidKeystroke)},
};
