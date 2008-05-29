/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2007 Mathieu Schroeter <mathieu.schroeter@gamesover.ch>
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
 * Foundation, Inc, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef X11_COMMON_H_
#define X11_COMMON_H_

#include <X11/Xlib.h>

typedef struct x11_s {
  Display *display;
  Window window;
  void *data;
} x11_t;


int x11_init (player_t *player);
void x11_uninit (player_t *player);

void x11_map (player_t *player);
void x11_unmap (player_t *player);

Display *x11_get_display (x11_t *x11);
Window x11_get_window (x11_t *x11);
void *x11_get_data (x11_t *x11);

#endif /* X11_COMMON_H_ */
