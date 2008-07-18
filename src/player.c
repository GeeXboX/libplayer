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
#include <string.h>
#include <inttypes.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"

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
  int res = PLAYER_INIT_ERROR;

  player = malloc (sizeof (player_t));
  player->type = type;
  player->verbosity = verbosity;
  player->mrl = NULL;  
  player->state = PLAYER_STATE_IDLE;
  player->loop = 0;
  player->shuffle = 0;
  player->ao = ao;
  player->vo = vo;
  player->x = 0;
  player->y = 0;
  player->w = 0;
  player->h = 0;
  player->aspect = 0.0;
  player->x11 = NULL;
  player->event_cb = event_cb;
  player->funcs = NULL;
  player->priv = NULL;

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

  if (!player->funcs || !player->priv)
  {
    player_uninit (player);
    return NULL;
  }

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

  if (player->mrl)
    mrl_list_free (player->mrl);

  /* free player specific private properties */
  if (player->funcs->uninit)
    player->funcs->uninit (player);

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

  return player->mrl;
}

void
player_mrl_set (player_t *player, mrl_t *mrl)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player || !mrl)
    return;

  if (player->mrl) {
    player_playback_stop (player);
    mrl->prev = player->mrl->prev;
    mrl->next = player->mrl->next;
    mrl_free (player->mrl, 0);
  }

  player->mrl = mrl;
}

void
player_mrl_append (player_t *player, mrl_t *mrl, player_mrl_add_t when)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player || !mrl)
    return;

  /* create/expand the playlist */
  if (!player->mrl) /* empty list */
    player->mrl = mrl;
  else /* create double-linked playlist, appending new MRL to the bottom */
  {
    mrl_t *list;

    list = player->mrl;
    while (list->next)
      list = list->next;
    list->next = mrl;
    mrl->prev = list;
  }

  /* play it now ? */
  if (when == PLAYER_MRL_ADD_NOW)
  {
    player_playback_stop (player);
    player->mrl = mrl;
    player_playback_start (player);
  }
}

void
player_mrl_remove (player_t *player)
{
  mrl_t *mrl, *mrl_p = NULL, *mrl_n = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  mrl = player->mrl;
  if (!mrl)
    return;

  mrl_p = mrl->prev;
  mrl_n = mrl->next;

  player_playback_stop (player);
  mrl_free (mrl, 0);

  /* link previous with the next and use the next as the current MRL */
  if (mrl_p && mrl_n) {
    mrl_p->next = mrl_n;
    mrl_n->prev = mrl_p;
    player->mrl = mrl_n;
  }
  /* use the previous as the current MRL */
  else if (mrl_p) {
    mrl_p->next = NULL;
    player->mrl = mrl_p;
  }
  /* use the next as the current MRL */
  else if (mrl_n) {
    mrl_n->prev = NULL;
    player->mrl = mrl_n;
  }
  else
    player->mrl = NULL;
}

void
player_mrl_remove_all (player_t *player)
{
  mrl_t *mrl;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  mrl = player->mrl;
  if (!mrl)
    return;

  player_playback_stop (player);

  mrl_list_free (mrl);
  player->mrl = NULL;
}

void
player_mrl_previous (player_t *player)
{
  mrl_t *mrl;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  mrl = player->mrl;
  if (!mrl || !mrl->prev)
    return;

  player_playback_stop (player);
  player->mrl = mrl->prev;
  player_playback_start (player);
}

void
player_mrl_next (player_t *player)
{
  mrl_t *mrl;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  mrl = player->mrl;
  if (!mrl || !mrl->next)
    return;

  player_playback_stop (player);
  player->mrl = mrl->next;
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
player_set_loop (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  player->loop = value;
}

void
player_set_shuffle (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  player->shuffle = value;
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

  if (!player->mrl) /* nothing to playback */
    return;

  mrl = player->mrl;
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
  if (player->event_cb)
    player->event_cb (PLAYER_EVENT_PLAYBACK_START, NULL);
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
  if (player->event_cb)
    player->event_cb (PLAYER_EVENT_PLAYBACK_STOP, NULL);
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

  /* player specific get_volume() */
  if (player->funcs->get_volume)
    res = player->funcs->get_volume (player);

  return res;
}

void
player_audio_volume_set (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific set_volume() */
  if (player->funcs->set_volume)
    player->funcs->set_volume (player, value);
}

player_mute_t
player_audio_mute_get (player_t *player)
{
  player_mute_t res = PLAYER_MUTE_UNKNOWN;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return res;

  /* player specific get_mute() */
  if (player->funcs->get_mute)
    res = player->funcs->get_mute (player);

  return res;
}

void
player_audio_mute_set (player_t *player, player_mute_t value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific set_mute() */
  if (player->funcs->set_mute)
    player->funcs->set_mute (player, value);
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
player_subtitle_set_delay (player_t *player, float value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific set_sub_delay() */
  if (player->funcs->set_sub_delay)
    player->funcs->set_sub_delay (player, value);
}

void
player_subtitle_set_alignment (player_t *player,
                               player_sub_alignment_t a)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific set_sub_alignment() */
  if (player->funcs->set_sub_alignment)
    player->funcs->set_sub_alignment (player, a);
}

void
player_subtitle_set_position (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific set_sub_pos() */
  if (player->funcs->set_sub_pos)
    player->funcs->set_sub_pos (player, value);
}

void
player_subtitle_set_visibility (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific set_sub_visibility() */
  if (player->funcs->set_sub_visibility)
    player->funcs->set_sub_visibility (player, value);
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
