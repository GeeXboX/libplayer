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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

static player_id_t player_id = PLAYER_ID_ALL;

static int
frontend_event_cb (player_event_t e, void *data)
{
  printf ("Received event type %d from player\n", e);
  return 0;
}

static void
do_regression_tests (player_t *player, char *name)
{
  mrl_t *mrl;
  mrl_resource_local_args_t *args;
  char *res;

  if (!player || !name)
    return;

  args = calloc (1, sizeof (mrl_resource_local_args_t));
  args->location = strdup (name);

  mrl = mrl_new (player, MRL_RESOURCE_FILE, args);
  if (!mrl)
    return;

  player_set_verbosity (player, PLAYER_MSG_INFO);
  player_mrl_set (player, mrl);
  mrl_get_type (player, NULL);
  mrl_get_resource (player, NULL);
  res = mrl_get_metadata (player, NULL, MRL_METADATA_TITLE);
  if (res)
    free (res);
  res = mrl_get_metadata_cd_track (player, NULL, 1, NULL);
  if (res)
    free (res);
  mrl_get_metadata_cd (player, NULL, MRL_METADATA_CD_DISCID);
  mrl_get_property (player, NULL, MRL_PROPERTY_SEEKABLE);
  res = mrl_get_audio_codec (player, NULL);
  if (res)
    free (res);
  res = mrl_get_video_codec (player, NULL);
  if (res)
    free (res);
  mrl_get_size (player, NULL);
  mrl = player_mrl_get_current (player);
  player_mrl_previous (player);
  player_mrl_next (player);
  player_get_time_pos (player);
  player_set_playback (player, PLAYER_PB_SINGLE);
  player_set_loop (player, PLAYER_LOOP_DISABLE, 0);
  player_set_shuffle (player, 0);
  player_set_framedrop (player, PLAYER_FRAMEDROP_DISABLE);
  player_playback_start (player);
  player_playback_seek (player, 2, PLAYER_PB_SEEK_RELATIVE);  /* 2s forward */
  player_playback_seek (player, -1, PLAYER_PB_SEEK_RELATIVE); /* 1s backward */
  player_playback_seek_chapter (player, 0, 0);
  player_playback_speed (player, 0.5);
  player_audio_volume_get (player);
  player_audio_volume_set (player, 85);
  player_audio_mute_get (player);
  player_audio_mute_set (player, PLAYER_MUTE_ON);
  player_audio_set_delay (player, 0, 0);
  player_audio_select (player, 1);
  player_audio_prev (player);
  player_audio_next (player);
  player_video_set_fullscreen (player, 1);
  player_video_set_aspect (player, PLAYER_VIDEO_ASPECT_BRIGHTNESS, 0, 0);
  player_video_set_panscan (player, 0, 0);
  player_video_set_aspect_ratio (player, 1.3333);
  player_subtitle_set_delay (player, 1.5);
  player_subtitle_set_alignment (player, PLAYER_SUB_ALIGNMENT_TOP);
  player_subtitle_set_position (player, 1);
  player_subtitle_set_visibility (player, 1);
  player_subtitle_scale (player, 1, 0);
  player_subtitle_select (player, 1);
  player_subtitle_prev (player);
  player_subtitle_next (player);
  player_dvd_nav (player, PLAYER_DVDNAV_MENU);
  player_dvd_angle_select (player, 1);
  player_dvd_angle_prev (player);
  player_dvd_angle_next (player);
  player_dvd_title_select (player, 1);
  player_dvd_title_prev (player);
  player_dvd_title_next (player);
  player_tv_channel_select (player, "S21");
  player_tv_channel_prev (player);
  player_tv_channel_next (player);
  player_radio_channel_select (player, "R1");
  player_radio_channel_prev (player);
  player_radio_channel_next (player);
  player_playback_pause (player);
  player_playback_stop (player);
  player_mrl_remove (player);
  player_mrl_remove_all (player);
}

static void
player_run_test (player_type_t player_type)
{
  player_t *player = NULL;

  player = player_init (player_type, PLAYER_AO_ALSA, PLAYER_VO_XV,
                        PLAYER_MSG_INFO, 0, frontend_event_cb);
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
    printf ("\n--- xine ---\n");
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

  pthread_exit (NULL);
}

int
main (int argc, char **argv)
{
  void *ret;
  pthread_attr_t attr;
  pthread_t tid;

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

  pthread_attr_init (&attr);
  pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_JOINABLE);

  pthread_create (&tid, &attr, player_test_thread, NULL);
  pthread_attr_destroy (&attr);

  pthread_join (tid, &ret);

  return 0;
}
