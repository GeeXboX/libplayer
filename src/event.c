/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
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

#include <stdlib.h>

#include "player.h"
#include "player_internals.h"
#include "supervisor.h"
#include "event_handler.h"

int
player_event_send (player_t *player, int e)
{
  int res;

  if (!player || !player->supervisor || !player->event)
    return -1;

  res = pl_event_handler_send (player->event, e);
  if (res)
    return res;

  /*
   * Release for event_handler; wait to recatch the supervisor to
   * finish the job.
   *
   * NOTE: recatch is ignored if the supervisor is not in a job.
   */
  pl_supervisor_sync_recatch (player, pl_event_handler_tid (player->event));

  return 0;
}
