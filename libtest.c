/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006 Benjamin Zores <ben@geexbox.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "player.h"

#define AUDIO_TEST_FILE "samples/audio.ogg"

static int
frontend_event_cb (player_event_t e, void *data)
{
  printf ("Received event type %d from player\n", e);
  return 0;
}

static void
do_regression_tests (player_t *player)
{
  if (!player)
    return;
  
  player_mrl_append (player, AUDIO_TEST_FILE,
                     PLAYER_MRL_TYPE_NONE, NULL, PLAYER_ADD_MRL_NOW);
  player_mrl_get_properties (player, player->mrl);
  player_mrl_get_metadata (player, player->mrl);
  printf ("Current volume: %d\n", player_get_volume (player));
  player_set_volume (player, 85);
  player_playback_start (player);
  player_playback_seek (player, 2);  /* 2s forward */
  player_playback_seek (player, -1); /* 1s backward */
  player_playback_pause (player);
  player_playback_stop (player);
  player_mrl_previous (player);
  player_mrl_next (player);
  player_uninit (player);
}

int
main (int argc, char **argv)
{
  player_t *player = NULL;
  
  printf ("*** libplayer regression tool ***\n");

  printf ("\n--- Dummy ---\n");
  player = player_init (PLAYER_TYPE_DUMMY, NULL, NULL, frontend_event_cb);
  do_regression_tests (player);
  
  printf ("\n--- Xine ---\n");
  player = player_init (PLAYER_TYPE_XINE, NULL, NULL, frontend_event_cb);
  do_regression_tests (player);

  return 0;
}
