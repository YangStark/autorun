/* Host test for the Switch pad as an XInput gamepad (dlls/xinput1_3/nx_pad.h). */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "xinput.h"
#include "../../dlls/xinput1_3/nx_pad.h"

int main(void)
{
    XINPUT_GAMEPAD gamepad;

    /* Nothing held, sticks centred. */
    memset( &gamepad, 0xcc, sizeof(gamepad) );
    nx_xinput_map( 0, 0, 0, 0, 0, &gamepad );
    assert( !gamepad.wButtons && !gamepad.bLeftTrigger && !gamepad.bRightTrigger );
    assert( !gamepad.sThumbLX && !gamepad.sThumbLY && !gamepad.sThumbRX && !gamepad.sThumbRY );

    /* Face buttons by position: the Switch's bottom B is Xbox A, and so on. */
    nx_xinput_map( NX_PAD_B, 0, 0, 0, 0, &gamepad );
    assert( gamepad.wButtons == XINPUT_GAMEPAD_A );
    nx_xinput_map( NX_PAD_A, 0, 0, 0, 0, &gamepad );
    assert( gamepad.wButtons == XINPUT_GAMEPAD_B );
    nx_xinput_map( NX_PAD_Y, 0, 0, 0, 0, &gamepad );
    assert( gamepad.wButtons == XINPUT_GAMEPAD_X );
    nx_xinput_map( NX_PAD_X, 0, 0, 0, 0, &gamepad );
    assert( gamepad.wButtons == XINPUT_GAMEPAD_Y );

    /* The rest, together. */
    nx_xinput_map( NX_PAD_UP | NX_PAD_DOWN | NX_PAD_LEFT | NX_PAD_RIGHT | NX_PAD_PLUS | NX_PAD_MINUS |
                   NX_PAD_STICKL | NX_PAD_STICKR | NX_PAD_L | NX_PAD_R, 0, 0, 0, 0, &gamepad );
    assert( gamepad.wButtons == (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT |
                                 XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK |
                                 XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB |
                                 XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER) );
    assert( !gamepad.bLeftTrigger && !gamepad.bRightTrigger );

    /* ZL and ZR are the triggers, fully pressed; a racing game accelerates with RT. */
    nx_xinput_map( NX_PAD_ZL | NX_PAD_ZR, 0, 0, 0, 0, &gamepad );
    assert( !gamepad.wButtons && gamepad.bLeftTrigger == 255 && gamepad.bRightTrigger == 255 );

    /* Sticks pass through, up positive as in XInput, clamped to a SHORT. */
    nx_xinput_map( 0, -32767, 32767, 12000, -40000, &gamepad );
    assert( gamepad.sThumbLX == -32767 && gamepad.sThumbLY == 32767 );
    assert( gamepad.sThumbRX == 12000 && gamepad.sThumbRY == -32768 );
    nx_xinput_map( 0, 40000, 0, 0, 0, &gamepad );
    assert( gamepad.sThumbLX == 32767 );

    /* Unix call parameters have no pointers: 32-bit and 64-bit layouts agree. */
    assert( sizeof(struct nx_xinput_state_params) == 24 && sizeof(struct nx_xinput_vibration_params) == 12 );

    puts( "XInput Switch pad: positional face buttons, d-pad, shoulders, triggers, sticks and call layout passed" );
    return 0;
}
