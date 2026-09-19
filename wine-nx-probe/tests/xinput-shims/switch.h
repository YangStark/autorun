#ifndef NX_XINPUT_TEST_SWITCH_H
#define NX_XINPUT_TEST_SWITCH_H

#include <stdint.h>

typedef uint64_t u64;
typedef struct PadState { unsigned int unused; } PadState;
typedef struct HidAnalogStickState { int x, y; } HidAnalogStickState;

enum
{
    HidNpadButton_A = 1ull << 0,
    HidNpadButton_B = 1ull << 1,
    HidNpadButton_X = 1ull << 2,
    HidNpadButton_Y = 1ull << 3,
    HidNpadButton_StickL = 1ull << 4,
    HidNpadButton_StickR = 1ull << 5,
    HidNpadButton_L = 1ull << 6,
    HidNpadButton_R = 1ull << 7,
    HidNpadButton_ZL = 1ull << 8,
    HidNpadButton_ZR = 1ull << 9,
    HidNpadButton_Plus = 1ull << 10,
    HidNpadButton_Minus = 1ull << 11,
    HidNpadButton_Left = 1ull << 12,
    HidNpadButton_Up = 1ull << 13,
    HidNpadButton_Right = 1ull << 14,
    HidNpadButton_Down = 1ull << 15,
    HidNpadStyleSet_NpadStandard = 1
};

void padConfigureInput(int count, int style);
void padInitializeDefault(PadState *pad);
void padUpdate(PadState *pad);
int padIsConnected(PadState *pad);
HidAnalogStickState padGetStickPos(PadState *pad, int index);
u64 padGetButtons(PadState *pad);
u64 armGetSystemTick(void);

#endif
