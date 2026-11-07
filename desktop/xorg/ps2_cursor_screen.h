/* SPDX-License-Identifier: MIT */
#ifndef PS2_XORG_CURSOR_H
#define PS2_XORG_CURSOR_H
#include "xorg-server.h"
#include "scrnintstr.h"
#include "mipointer.h"
#include "damage.h"
#include "ps2_context_link.h"
struct ps2_pipe_cursor;
struct ps2_cursor_screen {
    ScreenPtr screen;
    struct ps2_pipe_cursor *native;
    DamagePtr damage;
    CursorPtr current;
    DeviceIntPtr device;
    miPointerScreenFuncRec screen_funcs;
    CloseScreenProcPtr CloseScreen;
    ScreenBlockHandlerProcPtr BlockHandler;
    QueryBestSizeProcPtr QueryBestSize;
    Bool installed;
};
/* Install after Damage/native GC setup and before other pointer setup. One
 * submission-thread master pointer per screen, including its attached slave
 * input devices; positive monochrome cursors <=64x64.
 * miPointerInitialize owns the lower pointer CloseScreen. The outer native
 * hook restores the background and removes Damage/observer ownership first.
 * ARGB cursors are rejected, never routed to a CPU colour renderer.
 * Caller remains responsible for input drivers and DDX VT callbacks. */
Bool ps2_cursor_screen_prepare(ScreenPtr,struct ps2_cursor_screen *,
    struct pipe_context *,miPointerScreenFuncPtr);
Bool ps2_cursor_screen_suspend(struct ps2_cursor_screen *,Bool);
void ps2_cursor_screen_refresh(ScreenPtr);
#endif
