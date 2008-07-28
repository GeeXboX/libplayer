/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006-2008 Benjamin Zores <ben@geexbox.org>
 * Copyright (C) 2007-2008 Mathieu Schroeter <mathieu.schroeter@gamesover.ch>
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
#include <string.h>
#include <inttypes.h>
#include <pthread.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "playlist.h"
#include "event_handler.h"

/* players wrappers */
#include "wrapper_dummy.h"
#ifdef HAVE_XINE
#include "wrapper_xine.h"
#endif /* HAVE_XINE */
#ifdef HAVE_MPLAYER
#include "wrapper_mplayer.h"
#endif /* HAVE_MPLAYER */
#ifdef HAVE_VLC
#include "wrapper_vlc.h"
#endif /* HAVE_VLC */
#ifdef HAVE_GSTREAMER
#include "wrapper_gstreamer.h"
#endif /* HAVE_GSTREAMER */

#define MODULE_NAME "player"

int
player_event_cb (player_t *player, player_event_t e, void *data)
{
  if (!player)
    return -1;

  pthread_mutex_lock (&player->mutex_cb);

  /* send to the frontend event callback */
  if (player->event_cb)
    player->event_cb (e, data);

  pthread_mutex_unlock (&player->mutex_cb);

  return 0;
}

/***************************************************************************/
/*                                                                         */
/* Player (Un)Initialization                                               */
/*                                                                         */
/***************************************************************************/

player_t *
player_init (player_type_t type, player_ao_t ao, player_vo_t vo,
             player_verbosity_level_t verbosity,
             int event_cb (player_event_t e, void *data))
{
  player_t *player = NULL;
  init_status_t res = PLAYER_INIT_ERROR;
  int ret;

  player = calloc (1, sizeof (player_t));
  player->type = type;
  player->verbosity = verbosity;
  player->state = PLAYER_STATE_IDLE;
  player->ao = ao;
  player->vo = vo;
  player->event_cb = event_cb;
  player->playlist = playlist_new (0, 0, PLAYER_LOOP_DISABLE);

  switch (player->type)
  {
#ifdef HAVE_XINE
  case PLAYER_TYPE_XINE:
    player->funcs = register_functions_xine ();
    player->priv = register_private_xine ();
    break;
#endif /* HAVE_XINE */
#ifdef HAVE_MPLAYER
  case PLAYER_TYPE_MPLAYER:
    player->funcs = register_functions_mplayer ();
    player->priv = register_private_mplayer ();
    break;
#endif /* HAVE_MPLAYER */
#ifdef HAVE_VLC
  case PLAYER_TYPE_VLC:
    player->funcs = register_functions_vlc ();
    player->priv = register_private_vlc ();
    break;
#endif /* HAVE_VLC */
#ifdef HAVE_GSTREAMER
  case PLAYER_TYPE_GSTREAMER:
    player->funcs = register_functions_gstreamer ();
    player->priv = register_private_gstreamer ();
    break;
#endif /* HAVE_GSTREAMER */
  case PLAYER_TYPE_DUMMY:
    player->funcs = register_functions_dummy ();
    player->priv = register_private_dummy ();
    break;
  default:
    break;
  }

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  pthread_mutex_init (&player->mutex_cb, NULL);

  if (!player->funcs || !player->priv)
  {
    player_uninit (player);
    return NULL;
  }

  player->event = event_handler_register (player, NULL);
  if (!player->event)
  {
    player_uninit (player);
    return NULL;
  }

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "event_handler_init");
  ret = event_handler_init (player->event, NULL, NULL, NULL);
  if (ret)
  {
    plog (player, PLAYER_MSG_ERROR,
          MODULE_NAME, "failed to init event handler");
    player_uninit (player);
    return NULL;
  }

  event_handler_enable (player->event);

  player_set_verbosity (player, verbosity);

  /* player specific init */
  if (player->funcs->init)
    res = player->funcs->init (player);

  if (res != PLAYER_INIT_OK)
  {
    player_uninit (player);
    return NULL;
  }

  return player;
}

void
player_uninit (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* free player specific private properties */
  if (player->funcs->uninit)
    player->funcs->uninit (player);

  pthread_mutex_destroy (&player->mutex_cb);

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "event_handler_uninit");
  event_handler_disable (player->event);
  event_handler_uninit (player->event);

  playlist_free (player->playlist);
  free (player->funcs);
  free (player);
}

void
player_set_verbosity (player_t *player, player_verbosity_level_t level)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  player->verbosity = level;

  /* player specific verbosity level */
  if (player->funcs->set_verbosity)
    player->funcs->set_verbosity (player, level);
}

/***************************************************************************/
/*                                                                         */
/* Player to MRL connection                                                */
/*                                                                         */
/***************************************************************************/

mrl_t *
player_mrl_get_current (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return NULL;

  return playlist_get_mrl (player->playlist);
}

void
player_mrl_set (player_t *player, mrl_t *mrl)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player || !mrl)
    return;

  playlist_set_mrl (player->playlist, mrl);
}

void
player_mrl_append (player_t *player, mrl_t *mrl, player_mrl_add_t when)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player || !mrl)
    return;

  playlist_append_mrl (player->playlist, mrl);

  /* play it now ? */
  if (when == PLAYER_MRL_ADD_NOW)
  {
    player_playback_stop (player);
    playlist_last_mrl (player->playlist);
    player_playback_start (player);
  }
}

void
player_mrl_remove (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  playlist_remove_mrl (player->playlist);
}

void
player_mrl_remove_all (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  player_playback_stop (player);

  playlist_empty (player->playlist);
}

void
player_mrl_previous (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (!playlist_previous_mrl_available (player->playlist))
    return;

  player_playback_stop (player);
  playlist_previous_mrl (player->playlist);
  player_playback_start (player);
}

void
player_mrl_next (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (!playlist_next_mrl_available (player->playlist))
    return;

  player_playback_stop (player);
  playlist_next_mrl (player->playlist);
  player_playback_start (player);
}

/***************************************************************************/
/*                                                                         */
/* Player tuning & properties                                              */
/*                                                                         */
/***************************************************************************/

int
player_get_time_pos (player_t *player)
{
  int res = -1;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return -1;

  /* player specific get_time_pos() */
  if (player->funcs->get_time_pos)
    res = player->funcs->get_time_pos (player);

  return res;
}

void
player_set_loop (player_t *player, player_loop_t loop, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  playlist_set_loop (player->playlist, value, loop);
}

void
player_set_shuffle (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  playlist_set_shuffle (player->playlist, value);
}

void
player_set_framedrop (player_t *player, player_framedrop_t fd)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific set_framedrop() */
  if (player->funcs->set_framedrop)
    player->funcs->set_framedrop (player, fd);
}

/***************************************************************************/
/*                                                                         */
/* Playback related controls                                               */
/*                                                                         */
/***************************************************************************/

void
player_playback_start (player_t *player)
{
  mrl_t *mrl;
  mrl_properties_video_t *video;
  int res = PLAYER_PB_ERROR;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (player->state != PLAYER_STATE_IDLE) /* already running : stop it */
    player_playback_stop (player);

  mrl = playlist_get_mrl (player->playlist);
  if (!mrl) /* nothing to playback */
    return;

  if (mrl->prop && mrl->prop->video) {
    video = mrl->prop->video;
    player->w = video->width;
    player->h = video->height;
    player->aspect = video->aspect / PLAYER_VIDEO_ASPECT_RATIO_MULT;
  }

  /* player specific playback_start() */
  if (player->funcs->pb_start)
    res = player->funcs->pb_start (player);

  if (res != PLAYER_PB_OK)
    return;

  player->state = PLAYER_STATE_RUNNING;
#if 0
  plog (player, PLAYER_MSG_INFO,
        MODULE_NAME, "Now playing: %s", player->mrl->name);
#endif

  /* notify front-end */
  player_event_cb (player, PLAYER_EVENT_PLAYBACK_START, NULL);
}

void
player_playback_stop (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific playback_stop() */
  if (player->funcs->pb_stop)
    player->funcs->pb_stop (player);

  player->state = PLAYER_STATE_IDLE;

  /* notify front-end */
  player_event_cb (player, PLAYER_EVENT_PLAYBACK_STOP, NULL);
}

void
player_playback_pause (player_t *player)
{
  int res = PLAYER_PB_ERROR;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (player->state != PLAYER_STATE_PAUSE
      && player->state != PLAYER_STATE_RUNNING)
    return;

  /* player specific playback_pause() */
  if (player->funcs->pb_pause)
    res = player->funcs->pb_pause (player);

  if (res != PLAYER_PB_OK)
    return;

  if (player->state == PLAYER_STATE_RUNNING)
    player->state = PLAYER_STATE_PAUSE;
  else
    player->state = PLAYER_STATE_RUNNING;
}

void
player_playback_seek (player_t *player, int value, player_pb_seek_t seek)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific playback_seek() */
  if (player->funcs->pb_seek)
    player->funcs->pb_seek (player, value, seek);
}

void
player_playback_seek_chapter (player_t *player, int value, int absolute)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific playback_seek_chapter() */
  if (player->funcs->pb_seek_chapter)
    player->funcs->pb_seek_chapter (player, value, absolute);
}

void
player_playback_speed (player_t *player, float value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific playback_set_speed() */
  if (player->funcs->pb_set_speed)
    player->funcs->pb_set_speed (player, value);
}

/***************************************************************************/
/*                                                                         */
/* Audio related controls                                                  */
/*                                                                         */
/***************************************************************************/

int
player_audio_volume_get (player_t *player)
{
  int res = -1;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return -1;

  /* player specific audio_get_volume() */
  if (player->funcs->audio_get_volume)
    res = player->funcs->audio_get_volume (player);

  return res;
}

void
player_audio_volume_set (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_set_volume() */
  if (player->funcs->audio_set_volume)
    player->funcs->audio_set_volume (player, value);
}

player_mute_t
player_audio_mute_get (player_t *player)
{
  player_mute_t res = PLAYER_MUTE_UNKNOWN;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return res;

  /* player specific audio_get_mute() */
  if (player->funcs->audio_get_mute)
    res = player->funcs->audio_get_mute (player);

  return res;
}

void
player_audio_mute_set (player_t *player, player_mute_t value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_set_mute() */
  if (player->funcs->audio_set_mute)
    player->funcs->audio_set_mute (player, value);
}

void
player_audio_set_delay (player_t *player, int value, int absolute)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_set_delay() */
  if (player->funcs->audio_set_delay)
    player->funcs->audio_set_delay (player, value, absolute);
}

void
player_audio_select (player_t *player, int audio_id)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_select() */
  if (player->funcs->audio_select)
    player->funcs->audio_select (player, audio_id);
}

void
player_audio_prev (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_prev() */
  if (player->funcs->audio_prev)
    player->funcs->audio_prev (player);
}

void
player_audio_next (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_next() */
  if (player->funcs->audio_next)
    player->funcs->audio_next (player);
}

/***************************************************************************/
/*                                                                         */
/* Video related controls                                                  */
/*                                                                         */
/***************************************************************************/

void
player_video_set_fullscreen (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific video_set_fs() */
  if (player->funcs->video_set_fs)
    player->funcs->video_set_fs (player, value);
}

void
player_video_set_aspect (player_t *player, player_video_aspect_t aspect,
                         int8_t value, int absolute)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific video_set_aspect() */
  if (player->funcs->video_set_aspect)
    player->funcs->video_set_aspect (player, aspect, value, absolute);
}

void
player_video_set_panscan (player_t *player, int8_t value, int absolute)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific video_set_panscan() */
  if (player->funcs->video_set_panscan)
    player->funcs->video_set_panscan (player, value, absolute);
}

void
player_video_set_aspect_ratio (player_t *player, float value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific video_set_ar() */
  if (player->funcs->video_set_ar)
    player->funcs->video_set_ar (player, value);
}

/***************************************************************************/
/*                                                                         */
/* Subtitles related controls                                              */
/*                                                                         */
/***************************************************************************/

void
player_subtitle_set_delay (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_set_delay() */
  if (player->funcs->sub_set_delay)
    player->funcs->sub_set_delay (player, value);
}

void
player_subtitle_set_alignment (player_t *player,
                               player_sub_alignment_t a)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_set_alignment() */
  if (player->funcs->sub_set_alignment)
    player->funcs->sub_set_alignment (player, a);
}

void
player_subtitle_set_position (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_set_pos() */
  if (player->funcs->sub_set_pos)
    player->funcs->sub_set_pos (player, value);
}

void
player_subtitle_set_visibility (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_set_visibility() */
  if (player->funcs->sub_set_visibility)
    player->funcs->sub_set_visibility (player, value);
}

void
player_subtitle_scale (player_t *player, int value, int absolute)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_scale() */
  if (player->funcs->sub_scale)
    player->funcs->sub_scale (player, value, absolute);
}

void
player_subtitle_select (player_t *player, int sub_id)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_select() */
  if (player->funcs->sub_select)
    player->funcs->sub_select (player, sub_id);
}

void
player_subtitle_prev (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_prev() */
  if (player->funcs->sub_prev)
    player->funcs->sub_prev (player);
}

void
player_subtitle_next (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_next() */
  if (player->funcs->sub_next)
    player->funcs->sub_next (player);
}

/***************************************************************************/
/*                                                                         */
/* DVD specific controls                                                   */
/*                                                                         */
/***************************************************************************/

void
player_dvd_nav (player_t *player, player_dvdnav_t value)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific playback_dvdnav() */
  if (player->funcs->dvd_nav)
    player->funcs->dvd_nav (player, value);
}

void
player_dvd_angle_select (player_t *player, int angle)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_angle_set() */
  if (player->funcs->dvd_angle_set)
    player->funcs->dvd_angle_set (player, angle);
}

void
player_dvd_angle_prev (player_t *player)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_angle_prev() */
  if (player->funcs->dvd_angle_prev)
    player->funcs->dvd_angle_prev (player);
}

void
player_dvd_angle_next (player_t *player)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_angle_next() */
  if (player->funcs->dvd_angle_next)
    player->funcs->dvd_angle_next (player);
}

void
player_dvd_title_select (player_t *player, int title)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_title_set() */
  if (player->funcs->dvd_title_set)
    player->funcs->dvd_title_set (player, title);
}

void
player_dvd_title_prev (player_t *player)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_title_prev() */
  if (player->funcs->dvd_title_prev)
    player->funcs->dvd_title_prev (player);
}

void
player_dvd_title_next (player_t *player)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_title_next() */
  if (player->funcs->dvd_title_next)
    player->funcs->dvd_title_next (player);
}

/***************************************************************************/
/*                                                                         */
/* TV/DVB specific controls                                                */
/*                                                                         */
/***************************************************************************/

void
player_tv_channel_select (player_t *player, int channel)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVB && res != MRL_RESOURCE_TV)
    return;

  /* player specific tv_channel_set() */
  if (player->funcs->tv_channel_set)
    player->funcs->tv_channel_set (player, channel);
}

void
player_tv_channel_prev (player_t *player)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVB && res != MRL_RESOURCE_TV)
    return;

  /* player specific tv_channel_prev() */
  if (player->funcs->tv_channel_prev)
    player->funcs->tv_channel_prev (player);
}

void
player_tv_channel_next (player_t *player)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVB && res != MRL_RESOURCE_TV)
    return;

  /* player specific tv_channel_next() */
  if (player->funcs->tv_channel_next)
    player->funcs->tv_channel_next (player);
}

/***************************************************************************/
/*                                                                         */
/* Radio specific controls                                                 */
/*                                                                         */
/***************************************************************************/

void
player_radio_channel_select (player_t *player, int channel)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (mrl_get_resource (player, NULL) != MRL_RESOURCE_RADIO)
    return;

  /* player specific radio_channel_set() */
  if (player->funcs->radio_channel_set)
    player->funcs->radio_channel_set (player, channel);
}

void
player_radio_channel_prev (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (mrl_get_resource (player, NULL) != MRL_RESOURCE_RADIO)
    return;

  /* player specific radio_channel_prev() */
  if (player->funcs->radio_channel_prev)
    player->funcs->radio_channel_prev (player);
}

void
player_radio_channel_next (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (mrl_get_resource (player, NULL) != MRL_RESOURCE_RADIO)
    return;

  /* player specific radio_channel_next() */
  if (player->funcs->radio_channel_next)
    player->funcs->radio_channel_next (player);
}
