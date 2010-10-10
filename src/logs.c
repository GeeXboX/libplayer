/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006-2008 Benjamin Zores <ben@geexbox.org>
 * Copyright (C) 2008 Mathieu Schroeter <mathieu@schroetersa.ch>
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

int
pl_log_test (player_t *player, player_verbosity_level_t level)
{
  unsigned int verbosity;

  if (!player)
    return 0;

  pthread_mutex_lock (&player->mutex_verb);
  verbosity = player->verbosity;
  pthread_mutex_unlock (&player->mutex_verb);

  /* do we really want logging ? */
  if (verbosity == PLAYER_MSG_NONE)
    return 0;

  if (level < verbosity)
    return 0;

  return 1;
}

void
pl_log_orig (player_t *player,
             player_verbosity_level_t level, const char *format, ...)
{
#ifdef USE_LOGCOLOR
  static const char *const c[] = {
    [PLAYER_MSG_VERBOSE]  = F_BLUE,
    [PLAYER_MSG_INFO]     = F_GREEN,
    [PLAYER_MSG_WARNING]  = F_YELLOW,
    [PLAYER_MSG_ERROR]    = F_RED,
    [PLAYER_MSG_CRITICAL] = B_RED,
  };
#endif /* USE_LOGCOLOR */
  static const char *const l[] = {
    [PLAYER_MSG_VERBOSE]  = "Verb",
    [PLAYER_MSG_INFO]     = "Info",
    [PLAYER_MSG_WARNING]  = "Warn",
    [PLAYER_MSG_ERROR]    = "Err",
    [PLAYER_MSG_CRITICAL] = "Crit",
  };
  char fmt[256];
  va_list va;

  if (!player || !format)
    return;

  if (!pl_log_test (player, level))
    return;

#ifdef USE_LOGCOLOR
  snprintf (fmt, sizeof (fmt),
            "[" BOLD "libplayer/%%s" NORMAL "] %s%s" NORMAL ": %s\n",
            c[level], l[level], format);
#else
  snprintf (fmt, sizeof (fmt),
            "[libplayer/%%s] %s: %s\n", l[level], format);
#endif /* USE_LOGCOLOR */

  va_start (va, format);
  vfprintf (stderr, fmt, va);
  va_end (va);
}
