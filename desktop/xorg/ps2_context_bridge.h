/* SPDX-License-Identifier: MIT */
#ifndef PS2_CONTEXT_BRIDGE_H
#define PS2_CONTEXT_BRIDGE_H
#include "ps2_render_bridge.h"
#include "ps2_context_link.h"
struct ps2_exa_context_bridge {
    struct ps2_exa_render_bridge ordering;
    struct pipe_context *context;
    int attached;
};
/* Zero-initialize this structure. Install after surface preparation, before
 * texture preparation/exaDriverInit. No other callback owners may be inserted
 * into the table before this bridge is detached.
 * Cleanup restores every callback before releasing its Mesa lifetime lease.
 * The structure remains screen-owned until the screen is closed. */
Bool ps2_exa_context_bridge_prepare(ScreenPtr,struct ps2_exa_context_bridge *,
    struct ps2_surface_exa *,struct pipe_context *);
void ps2_exa_context_bridge_cleanup(struct ps2_exa_context_bridge *);
#endif
