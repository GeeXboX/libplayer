/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006-2008 Benjamin Zores <ben@geexbox.org>
 * Copyright (C) 2008 Mathieu Schroeter <mathieu.schroeter@gamesover.ch>
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

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "player.h"
#include "player_internals.h"

#ifdef USE_LOGCOLOR
#define NORMAL   "\033[0m"
#define COLOR(x) "\033[" #x ";1m"
#define BOLD     COLOR(1)
#define F_RED    COLOR(31)
#define F_GREEN  COLOR(32)
#define F_YELLOW COLOR(33)
#define F_BLUE   COLOR(34)
#define B_RED    COLOR(41)
#endif /* USE_LOGCOLOR */

void
plog (player_t *player, player_verbosity_level_t level,
      const char *module, const char *format, ...)
{
#ifdef USE_LOGCOLOR
  static const char const *c[] = {
    [PLAYER_MSG_VERBOSE]  = F_BLUE,
    [PLAYER_MSG_INFO]     = F_GREEN,
    [PLAYER_MSG_WARNING]  = F_YELLOW,
    [PLAYER_MSG_ERROR]    = F_RED,
    [PLAYER_MSG_CRITICAL] = B_RED,
  };
#endif /* USE_LOGCOLOR */
  static const char const *l[] = {
    [PLAYER_MSG_VERBOSE]  = "Verb",
    [PLAYER_MSG_INFO]     = "Info",
    [PLAYER_MSG_WARNING]  = "Warn",
    [PLAYER_MSG_ERROR]    = "Err",
    [PLAYER_MSG_CRITICAL] = "Crit",
  };
  va_list va;
  int verbosity;

  if (!player || !format)
    return;

  pthread_mutex_lock (&player->mutex_verb);
  verbosity = player->verbosity;
  pthread_mutex_unlock (&player->mutex_verb);

  /* do we really want loging ? */
  if (verbosity == PLAYER_MSG_NONE)
    return;

  if (level < verbosity)
    return;

  va_start (va, format);

#ifdef USE_LOGCOLOR
  fprintf (stderr, "[" BOLD "%s" NORMAL "] %s%s" NORMAL ": ",
           module, c[level], l[level]);
#else
  fprintf (stderr, "[%s] %s: ", module, l[level]);
#endif /* USE_LOGCOLOR */

  vfprintf (stderr, format, va);
  fprintf (stderr, "\n");
  va_end (va);
}
