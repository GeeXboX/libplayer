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

#ifndef PLAYER_WINDOW_H
#define PLAYER_WINDOW_H

#define WIN_PROPERTY_X (1 << 0)
#define WIN_PROPERTY_Y (1 << 1)
#define WIN_PROPERTY_W (1 << 2)
#define WIN_PROPERTY_H (1 << 3)

typedef enum window_vdpau_caps {
  WIN_VDPAU_MPEG1   = (1 << 0),
  WIN_VDPAU_MPEG2   = (1 << 1),
  WIN_VDPAU_H264    = (1 << 2),
  WIN_VDPAU_VC1     = (1 << 3),
  WIN_VDPAU_MPEG4P2 = (1 << 4),
  WIN_VDPAU_DIVX4   = (1 << 5),
  WIN_VDPAU_DIVX5   = (1 << 6),
} window_vdpau_caps_t;

typedef struct window_s window_t;


int pl_window_init (window_t *win);
void pl_window_uninit (window_t *win);
void pl_window_map (window_t *win);
void pl_window_unmap (window_t *win);
void pl_window_resize (window_t *win);
uint32_t pl_window_winid_get (window_t *win);
void *pl_window_data_get (window_t *win);
void pl_window_video_pos_get (window_t *win, int *x, int *y);
void pl_window_win_props_set (window_t *win,
                              int x, int y, int w, int h, int flags);
int pl_window_vdpau_caps_get (window_t *win);

window_t *pl_window_register (player_t *player);
void pl_window_destroy (window_t *win);

#endif /* PLAYER_WINDOW_H */
