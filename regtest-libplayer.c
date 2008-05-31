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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#include "player.h"

#define AUDIO_TEST_FILE "samples/audio.ogg"
#define VIDEO_TEST_FILE "samples/background.avi"

typedef enum player_id {
  PLAYER_ID_ALL = 0,
  PLAYER_ID_XINE,
  PLAYER_ID_MPLAYER,
  PLAYER_ID_VLC,
  PLAYER_ID_GSTREAMER,
  PLAYER_ID_DUMMY
} player_id_t;

static pthread_t tid;
static player_id_t player_id = PLAYER_ID_ALL;

static void
break_down (int s)
{
  exit (0);
}

static int
frontend_event_cb (player_event_t e, void *data)
{
  printf ("Received event type %d from player\n", e);
  return 0;
}

static void
do_regression_tests (player_t *player, char *mrl)
{
  if (!player || !mrl)
    return;

  player_mrl_append (player, mrl, NULL, PLAYER_ADD_MRL_NOW);
  player_mrl_get_property (player, player_get_mrl (player), PLAYER_PROPERTY_SEEKABLE);
  player_mrl_get_metadata (player, player_get_mrl (player), PLAYER_METADATA_TITLE);
  printf ("Current volume: %d\n", player_get_volume (player));
  player_set_volume (player, 85);
  player_playback_start (player);
  player_playback_seek (player, 2);  /* 2s forward */
  player_playback_seek (player, -1); /* 1s backward */
  player_mute_t mute = player_get_mute (player);
  printf ("Current mute: %s\n", mute == PLAYER_MUTE_ON
                                ? "on" : (mute == PLAYER_MUTE_OFF
                                          ? "off" : "unknown"));
  printf ("Current time position: %d [ms]\n", player_get_time_pos (player));
  player_set_mute (player, PLAYER_MUTE_ON);
  player_set_sub_delay (player, 1.5);
  player_playback_pause (player);
  player_playback_stop (player);
  player_mrl_previous (player);
  player_mrl_next (player);
  player_mrl_remove (player);
  player_mrl_remove_all (player);
}

static void
player_run_test (player_type_t player_type)
{
  player_t *player = NULL;

  player = player_init (player_type, PLAYER_AO_ALSA, PLAYER_VO_XV,
                        PLAYER_MSG_INFO, frontend_event_cb);
  do_regression_tests (player, AUDIO_TEST_FILE);
  do_regression_tests (player, VIDEO_TEST_FILE);
  player_uninit (player);
}

static void *
player_test_thread (void *cookie)
{
  if (player_id == PLAYER_ID_DUMMY || player_id == PLAYER_ID_ALL)
  {
    printf ("\n--- Dummy ---\n");
    player_run_test (PLAYER_TYPE_DUMMY);
  }

#ifdef HAVE_XINE
  if (player_id == PLAYER_ID_XINE || player_id == PLAYER_ID_ALL)
  {
    printf ("\n--- Xine ---\n");
    player_run_test (PLAYER_TYPE_XINE);
  }
#endif /* HAVE_XINE */

#ifdef HAVE_MPLAYER
  if (player_id == PLAYER_ID_MPLAYER || player_id == PLAYER_ID_ALL)
  {
    printf ("\n--- MPlayer ---\n");
    player_run_test (PLAYER_TYPE_MPLAYER);
  }
#endif /* HAVE_MPLAYER */

#ifdef HAVE_VLC
  if (player_id == PLAYER_ID_VLC || player_id == PLAYER_ID_ALL)
  {
    printf ("\n--- VLC ---\n");
    player_run_test (PLAYER_TYPE_VLC);
  }
#endif /* HAVE_VLC */

#ifdef HAVE_GSTREAMER
  if (player_id == PLAYER_ID_GSTREAMER || player_id == PLAYER_ID_ALL)
  {
    printf ("\n--- GSTREAMER ---\n");
    player_run_test (PLAYER_TYPE_GSTREAMER);
  }
#endif /* HAVE_GSTREAMER */

  break_down (-1);
  return NULL;
}

int
main (int argc, char **argv)
{
  printf ("*** libplayer %s regression tool ***\n", LIBPLAYER_VERSION);

  if (argc > 1)
  {
    if (!strcmp (argv[1], "all"))
      player_id = PLAYER_ID_ALL;
#ifdef HAVE_XINE
    else if (!strcmp (argv[1], "xine"))
      player_id = PLAYER_ID_XINE;
#endif /* HAVE_XINE */
#ifdef HAVE_MPLAYER
    else if (!strcmp (argv[1], "mplayer"))
      player_id = PLAYER_ID_MPLAYER;
#endif /* HAVE_MPLAYER */
#ifdef HAVE_VLC
    else if (!strcmp (argv[1], "vlc"))
      player_id = PLAYER_ID_VLC;
#endif /* HAVE_VLC */
#ifdef HAVE_GSTREAMER
    else if (!strcmp (argv[1], "gstreamer"))
      player_id = PLAYER_ID_GSTREAMER;
#endif /* HAVE_GSTREAMER */
    else if (!strcmp (argv[1], "dummy"))
      player_id = PLAYER_ID_DUMMY;
    else
    {
      printf ("unknown or invalid player specified.\n");
      return -1;
    }
  }

  signal (SIGINT, break_down);
  pthread_create (&tid, NULL, player_test_thread, NULL);

  while (1)
    sleep (1000000);

  /* we should never goes there */
  return 0;
}
