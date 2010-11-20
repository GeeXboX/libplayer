/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2010 Mathieu Schroeter <mathieu@schroetersa.ch>
 *
 * This file is part of libplayer.
 *
 * libplayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libplayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libplayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdlib.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"

#ifdef HAVE_WIN_XCB
#include "window_xcb.h"
#endif /* HAVE_WIN_XCB */

#include "window_common.h"
#include "window.h"

#define MODULE_NAME "window"


int
pl_window_init (window_t *vo)
{
  if (!vo)
    return -1;

  pl_log (vo->player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  return vo->funcs ? vo->funcs->init (vo) : -1;
}

void
pl_window_uninit (window_t *vo)
{
  if (!vo)
    return;

  pl_log (vo->player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (vo->funcs)
    vo->funcs->uninit (vo);
}

void
pl_window_map (window_t *vo)
{
  if (!vo)
    return;

  pl_log (vo->player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (vo->funcs)
    vo->funcs->map (vo);
}

void
pl_window_unmap (window_t *vo)
{
  if (!vo)
    return;

  pl_log (vo->player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (vo->funcs)
    vo->funcs->unmap (vo);
}

void
pl_window_resize (window_t *vo)
{
  if (!vo)
    return;

  pl_log (vo->player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (vo->funcs)
    vo->funcs->resize (vo);
}

uint32_t
pl_window_winid_get (window_t *vo)
{
  if (!vo)
    return 0;

  pl_log (vo->player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  return vo->funcs ? vo->funcs->winid_get (vo) : 0;
}

void *
pl_window_data_get (window_t *vo)
{
  if (!vo)
    return NULL;

  pl_log (vo->player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  return vo->funcs ? vo->funcs->data_get (vo) : NULL;
}

void
pl_window_video_pos_get (window_t *vo, int *x, int *y)
{
  if (!vo)
    return;

  pl_log (vo->player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (vo->funcs)
    vo->funcs->video_pos_get (vo, x, y);
}

void
pl_window_win_props_set (window_t *vo,
                           int x, int y, int w, int h, int flags)
{
  if (!vo)
    return;

  pl_log (vo->player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (vo->funcs)
    vo->funcs->win_props_set (vo, x, y, w, h, flags);
}

int
pl_window_vdpau_caps_get (window_t *vo)
{
  if (!vo)
    return 0;

  pl_log (vo->player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  return vo->funcs ? vo->funcs->vdpau_caps_get (vo) : 0;
}

window_t *
pl_window_register (player_t *player)
{
  static const player_vo_t backend[] = {
    [PLAYER_VO_AUTO]      = WIN_BACKEND_AUTO,
    [PLAYER_VO_NULL]      = WIN_BACKEND_NULL,
    [PLAYER_VO_X11]       = WIN_BACKEND_XCB,
    [PLAYER_VO_X11_SDL]   = WIN_BACKEND_XCB,
    [PLAYER_VO_XV]        = WIN_BACKEND_XCB,
    [PLAYER_VO_GL]        = WIN_BACKEND_XCB,
    [PLAYER_VO_FB]        = WIN_BACKEND_NULL,
    [PLAYER_VO_DIRECTFB]  = WIN_BACKEND_NULL,
    [PLAYER_VO_VDPAU]     = WIN_BACKEND_XCB,
    [PLAYER_VO_VAAPI]     = WIN_BACKEND_XCB,
  };
  window_t *vo;
  window_funcs_t *funcs = NULL;

  if (player->vo >= ARRAY_NB_ELEMENTS (backend))
    return NULL;

  switch (backend[player->vo])
  {
  /* Get the first backend available. */
  case WIN_BACKEND_AUTO:

#ifdef HAVE_WIN_XCB
  case WIN_BACKEND_XCB:
    funcs = pl_window_xcb_register ();
    break;
#endif /* HAVE_WIN_XCB */

  case WIN_BACKEND_GDI:
  case WIN_BACKEND_NULL:
  default:
    break;
  }

  if (!funcs)
  {
    pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "No window backend");
    return NULL;
  }

  vo = PCALLOC (window_t, 1);
  if (!vo)
  {
    PFREE (funcs);
    return NULL;
  }

  vo->player  = player;
  vo->funcs   = funcs;
  vo->backend = backend[player->vo];
  return vo;
}

void
pl_window_destroy (window_t *vo)
{
  if (!vo)
    return;

  PFREE (vo->funcs);
  PFREE (vo);
}
