/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006 Benjamin Zores <ben@geexbox.org>
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

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "player.h"
#include "player_internals.h"

void
plog (player_t *player, player_verbosity_level_t level,
      const char *module, const char *format, ...)
{
  va_list va;

  if (!player || !format)
    return;

  /* do we really want loging ? */
  if (player->verbosity == PLAYER_MSG_NONE)
    return;

  if (level < player->verbosity)
    return;

  va_start (va, format);
  fprintf (stderr, "[%s]: ", module);
  vfprintf (stderr, format, va);
  fprintf (stderr, "\n");
  va_end (va);
}
