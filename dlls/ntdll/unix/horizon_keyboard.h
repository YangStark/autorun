/*
 * Keyboard input for the Horizon server, as wineserver queues it
 * (server/queue.c: queue_keyboard_message, rawkeyboard_init, update_key_state).
 *
 * The console has no keyboard: the controller's keys arrive through
 * NtUserSendHardwareInput. Each becomes the key message a window receives and,
 * for a process registered for raw keyboard input, WM_INPUT - which is the only
 * way DirectInput 8 reads the keyboard.
 */

#ifndef __WINE_HORIZON_KEYBOARD_H
#define __WINE_HORIZON_KEYBOARD_H

#define HORIZON_KBD_WM_KEYDOWN          0x0100
#define HORIZON_KBD_WM_KEYUP            0x0101
#define HORIZON_KBD_WM_SYSKEYDOWN       0x0104
#define HORIZON_KBD_WM_SYSKEYUP         0x0105
#define HORIZON_KBD_VK_SHIFT            0x10
#define HORIZON_KBD_VK_CONTROL          0x11
#define HORIZON_KBD_VK_MENU             0x12
#define HORIZON_KBD_VK_F10              0x79
#define HORIZON_KBD_VK_LSHIFT           0xa0
#define HORIZON_KBD_VK_RSHIFT           0xa1
#define HORIZON_KBD_VK_LCONTROL         0xa2
#define HORIZON_KBD_VK_RCONTROL         0xa3
#define HORIZON_KBD_VK_LMENU            0xa4
#define HORIZON_KBD_VK_RMENU            0xa5
#define HORIZON_KEYEVENTF_EXTENDEDKEY   0x0001
#define HORIZON_KEYEVENTF_KEYUP         0x0002
#define HORIZON_KF_EXTENDED             0x0100
#define HORIZON_KF_ALTDOWN              0x2000
#define HORIZON_KF_REPEAT               0x4000
#define HORIZON_KF_UP                   0x8000
#define HORIZON_RI_KEY_BREAK            0x0001
#define HORIZON_RI_KEY_E0               0x0002
#define HORIZON_RIM_TYPEKEYBOARD        1
#define HORIZON_WINE_KEYBOARD_HANDLE    2
/* struct rawinput_device.usage for the keyboard: MAKELONG(HID_USAGE_GENERIC_KEYBOARD, HID_USAGE_PAGE_GENERIC) */
#define HORIZON_RAWINPUT_USAGE_KEYBOARD 0x00010006
#define HORIZON_RIDEV_NOLEGACY          0x00000030

/* RAWKEYBOARD */
struct horizon_raw_keyboard
{
    unsigned short make_code;
    unsigned short flags;
    unsigned short reserved;
    unsigned short vkey;
    unsigned int message;
    unsigned int extra_information;
};

/* struct rawinput_device, as update_rawinput_devices sends it */
struct horizon_rawinput_device
{
    unsigned int usage;
    unsigned int flags;
    unsigned int target;
};

struct horizon_key_event
{
    unsigned int message;             /* WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN or WM_SYSKEYUP */
    unsigned int vkey;                /* wParam; left or right for a modifier, which win32u makes generic */
    unsigned long long lparam;
    unsigned int data_flags;          /* hardware_msg_data.flags */
    struct horizon_raw_keyboard raw;  /* the WM_INPUT payload */
};

/* One key event. keystate is the desktop's key state before the event;
 * alt_pressed is wineserver's desktop->alt_pressed, telling WM_SYSKEYUP from
 * WM_KEYUP when Alt is released. */
static inline void horizon_keyboard_event( const unsigned char *keystate, int *alt_pressed,
                                           unsigned short input_vkey, unsigned short scan,
                                           unsigned int flags, unsigned int info,
                                           struct horizon_key_event *event )
{
    unsigned char vkey = (unsigned char)input_vkey;
    unsigned int kf = 0, up = flags & HORIZON_KEYEVENTF_KEYUP;

    switch (vkey)
    {
    case HORIZON_KBD_VK_MENU: case HORIZON_KBD_VK_LMENU: case HORIZON_KBD_VK_RMENU:
        vkey = (flags & HORIZON_KEYEVENTF_EXTENDEDKEY) ? HORIZON_KBD_VK_RMENU : HORIZON_KBD_VK_LMENU;
        break;
    case HORIZON_KBD_VK_CONTROL: case HORIZON_KBD_VK_LCONTROL: case HORIZON_KBD_VK_RCONTROL:
        vkey = (flags & HORIZON_KEYEVENTF_EXTENDEDKEY) ? HORIZON_KBD_VK_RCONTROL : HORIZON_KBD_VK_LCONTROL;
        break;
    case HORIZON_KBD_VK_SHIFT: case HORIZON_KBD_VK_LSHIFT: case HORIZON_KBD_VK_RSHIFT:
        vkey = (flags & HORIZON_KEYEVENTF_EXTENDEDKEY) ? HORIZON_KBD_VK_RSHIFT : HORIZON_KBD_VK_LSHIFT;
        break;
    }

    event->message = up ? HORIZON_KBD_WM_KEYUP : HORIZON_KBD_WM_KEYDOWN;
    switch (vkey)
    {
    case HORIZON_KBD_VK_LMENU: case HORIZON_KBD_VK_RMENU:
        if (up)
        {
            /* WM_SYSKEYUP if Alt is still down and no other key came in between */
            if (!(keystate[HORIZON_KBD_VK_MENU] & 0x80) || !*alt_pressed) break;
            event->message = HORIZON_KBD_WM_SYSKEYUP;
            *alt_pressed = 0;
        }
        else
        {
            /* WM_SYSKEYDOWN for Alt, except with Ctrl */
            if (keystate[HORIZON_KBD_VK_CONTROL] & 0x80) break;
            event->message = HORIZON_KBD_WM_SYSKEYDOWN;
            *alt_pressed = 1;
        }
        break;
    case HORIZON_KBD_VK_LCONTROL: case HORIZON_KBD_VK_RCONTROL:
        /* WM_SYSKEYUP on release while Alt is down */
        if (!up || !(keystate[HORIZON_KBD_VK_MENU] & 0x80)) break;
        event->message = HORIZON_KBD_WM_SYSKEYUP;
        *alt_pressed = 0;
        break;
    default:
        /* WM_SYSKEY* for Alt with any other key, and for F10 */
        if (keystate[HORIZON_KBD_VK_CONTROL] & 0x80) break;
        if (!(keystate[HORIZON_KBD_VK_MENU] & 0x80)) break;
        /* fall through */
    case HORIZON_KBD_VK_F10:
        event->message = up ? HORIZON_KBD_WM_SYSKEYUP : HORIZON_KBD_WM_SYSKEYDOWN;
        *alt_pressed = 0;
        break;
    }

    if (flags & HORIZON_KEYEVENTF_EXTENDEDKEY) kf |= HORIZON_KF_EXTENDED;
    if (up) kf |= HORIZON_KF_REPEAT | HORIZON_KF_UP;
    else if (keystate[vkey] & 0x80) kf |= HORIZON_KF_REPEAT;
    event->vkey = vkey;
    event->lparam = (unsigned long long)kf << 16 | (unsigned long long)(scan & 0xff) << 16 | 1;
    event->data_flags = (kf & (HORIZON_KF_EXTENDED | HORIZON_KF_ALTDOWN | HORIZON_KF_UP)) >> 8;

    event->raw.make_code = scan;
    event->raw.flags = up ? HORIZON_RI_KEY_BREAK : 0;
    if (flags & HORIZON_KEYEVENTF_EXTENDEDKEY) event->raw.flags |= HORIZON_RI_KEY_E0;
    event->raw.reserved = 0;
    switch (vkey)
    {
    case HORIZON_KBD_VK_LSHIFT: case HORIZON_KBD_VK_RSHIFT:
        event->raw.vkey = HORIZON_KBD_VK_SHIFT;
        event->raw.flags &= ~HORIZON_RI_KEY_E0;
        break;
    case HORIZON_KBD_VK_LCONTROL: case HORIZON_KBD_VK_RCONTROL:
        event->raw.vkey = HORIZON_KBD_VK_CONTROL;
        break;
    case HORIZON_KBD_VK_LMENU: case HORIZON_KBD_VK_RMENU:
        event->raw.vkey = HORIZON_KBD_VK_MENU;
        break;
    default:
        event->raw.vkey = vkey;
        break;
    }
    event->raw.message = event->message;
    event->raw.extra_information = info;
}

static inline void horizon_keyboard_set( unsigned char *keystate, unsigned char key, unsigned char down )
{
    if (down)
    {
        if (!(keystate[key] & 0x80)) keystate[key] ^= 0x01;
        keystate[key] |= down;
    }
    else keystate[key] &= ~0x80;
}

/* The key state after a key message: down is 0xc0 for the desktop's
 * (asynchronous) state and 0x80 for a thread's. */
static inline void horizon_keyboard_update_state( unsigned char *keystate, unsigned int message,
                                                  unsigned int vkey, unsigned char down )
{
    unsigned char key = (unsigned char)vkey;

    if (message == HORIZON_KBD_WM_KEYUP || message == HORIZON_KBD_WM_SYSKEYUP) down = 0;
    horizon_keyboard_set( keystate, key, down );
    switch (key)
    {
    case HORIZON_KBD_VK_LCONTROL: case HORIZON_KBD_VK_RCONTROL:
        horizon_keyboard_set( keystate, HORIZON_KBD_VK_CONTROL,
                              (keystate[HORIZON_KBD_VK_LCONTROL] | keystate[HORIZON_KBD_VK_RCONTROL]) & 0x80 );
        break;
    case HORIZON_KBD_VK_LMENU: case HORIZON_KBD_VK_RMENU:
        horizon_keyboard_set( keystate, HORIZON_KBD_VK_MENU,
                              (keystate[HORIZON_KBD_VK_LMENU] | keystate[HORIZON_KBD_VK_RMENU]) & 0x80 );
        break;
    case HORIZON_KBD_VK_LSHIFT: case HORIZON_KBD_VK_RSHIFT:
        horizon_keyboard_set( keystate, HORIZON_KBD_VK_SHIFT,
                              (keystate[HORIZON_KBD_VK_LSHIFT] | keystate[HORIZON_KBD_VK_RSHIFT]) & 0x80 );
        break;
    }
}

static inline const struct horizon_rawinput_device *horizon_rawinput_find(
    const struct horizon_rawinput_device *devices, unsigned int count, unsigned int usage )
{
    unsigned int i;

    for (i = 0; i < count; i++)
        if (devices[i].usage == usage) return &devices[i];
    return 0;
}

#endif /* __WINE_HORIZON_KEYBOARD_H */
