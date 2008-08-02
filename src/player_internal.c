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

#include <pthread.h>
#include <stdlib.h>
#include <inttypes.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "playlist.h"
#include "event.h"

#define MODULE_NAME "player"

/***************************************************************************/
/*                                                                         */
/* Player (Un)Initialization                                               */
/*                                                                         */
/***************************************************************************/

init_status_t
player_sv_init (player_t *player)
{
  init_status_t res = PLAYER_INIT_ERROR;

  if (!player)
    return res;

  /* player specific init */
  if (player->funcs->init)
    res = player->funcs->init (player);

  return res;
}

void
player_sv_uninit (player_t *player)
{
  if (!player)
    return;

  /* free player specific private properties */
  if (player->funcs->uninit)
    player->funcs->uninit (player);
}

void
player_sv_set_verbosity (player_t *player, player_verbosity_level_t level)
{
  if (!player)
    return;

  pthread_mutex_lock (&player->mutex_verb);
  player->verbosity = level;
  pthread_mutex_unlock (&player->mutex_verb);

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
player_sv_mrl_get_current (player_t *player)
{
  if (!player)
    return NULL;

  return playlist_get_mrl (player->playlist);
}

void
player_sv_mrl_set (player_t *player, mrl_t *mrl)
{
  if (!player || !mrl)
    return;

  playlist_set_mrl (player->playlist, mrl);
}

void
player_sv_mrl_append (player_t *player, mrl_t *mrl, player_mrl_add_t when)
{
  if (!player || !mrl)
    return;

  playlist_append_mrl (player->playlist, mrl);

  /* play it now ? */
  if (when == PLAYER_MRL_ADD_NOW)
  {
    player_sv_playback_stop (player);
    playlist_last_mrl (player->playlist);
    player_sv_playback_start (player);
  }
}

void
player_sv_mrl_remove (player_t *player)
{
  if (!player)
    return;

  playlist_remove_mrl (player->playlist);
}

void
player_sv_mrl_remove_all (player_t *player)
{
  if (!player)
    return;

  player_sv_playback_stop (player);
  playlist_empty (player->playlist);
}

void
player_sv_mrl_previous (player_t *player)
{
  if (!player)
    return;

  if (!playlist_previous_mrl_available (player->playlist))
    return;

  player_sv_playback_stop (player);
  playlist_previous_mrl (player->playlist);
  player_sv_playback_start (player);
}

void
player_sv_mrl_next (player_t *player)
{
  if (!player)
    return;

  if (!playlist_next_mrl_available (player->playlist))
    return;

  player_sv_playback_stop (player);
  playlist_next_mrl (player->playlist);
  player_sv_playback_start (player);
}

void
player_sv_mrl_next_play (player_t *player)
{
  if (!player)
    return;

  player_sv_playback_stop (player);

  if (!playlist_next_play (player->playlist))
    return;

  player_sv_playback_start (player);
}

/***************************************************************************/
/*                                                                         */
/* Player tuning & properties                                              */
/*                                                                         */
/***************************************************************************/

int
player_sv_get_time_pos (player_t *player)
{
  int res = -1;

  if (!player)
    return -1;

  /* player specific get_time_pos() */
  if (player->funcs->get_time_pos)
    res = player->funcs->get_time_pos (player);

  return res;
}

void
player_sv_set_playback (player_t *player, player_pb_t pb)
{
  if (!player)
    return;

  player->pb_mode = pb;
}

void
player_sv_set_loop (player_t *player, player_loop_t loop, int value)
{
  if (!player)
    return;

  if (player->pb_mode != PLAYER_PB_AUTO && loop != PLAYER_LOOP_DISABLE)
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "loop is only activated with PLAYBACK_AUTO mode");

  playlist_set_loop (player->playlist, value, loop);
}

void
player_sv_set_shuffle (player_t *player, int value)
{
  if (!player)
    return;

  playlist_set_shuffle (player->playlist, value);
}

void
player_sv_set_framedrop (player_t *player, player_framedrop_t fd)
{
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
player_sv_playback_start (player_t *player)
{
  mrl_t *mrl;
  int res = PLAYER_PB_ERROR;

  if (!player)
    return;

  if (player->state != PLAYER_STATE_IDLE) /* already running : stop it */
    player_sv_playback_stop (player);

  mrl = playlist_get_mrl (player->playlist);
  if (!mrl) /* nothing to playback */
    return;

  if (mrl->prop && mrl->prop->video)
  {
    mrl_properties_video_t *video = mrl->prop->video;
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

  /* notify front-end */
  player_event_send (player, PLAYER_EVENT_PLAYBACK_START, NULL);
}

void
player_sv_playback_stop (player_t *player)
{
  if (!player)
    return;

  if (player->state == PLAYER_STATE_IDLE)
    return; /* not running */

  /* player specific playback_stop() */
  if (player->funcs->pb_stop)
    player->funcs->pb_stop (player);

  player->state = PLAYER_STATE_IDLE;

  /* notify front-end */
  player_event_send (player, PLAYER_EVENT_PLAYBACK_STOP, NULL);
}

void
player_sv_playback_pause (player_t *player)
{
  int res = PLAYER_PB_ERROR;

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
player_sv_playback_seek (player_t *player, int value, player_pb_seek_t seek)
{
  if (!player)
    return;

  /* player specific playback_seek() */
  if (player->funcs->pb_seek)
    player->funcs->pb_seek (player, value, seek);
}

void
player_sv_playback_seek_chapter (player_t *player, int value, int absolute)
{
  if (!player)
    return;

  /* player specific playback_seek_chapter() */
  if (player->funcs->pb_seek_chapter)
    player->funcs->pb_seek_chapter (player, value, absolute);
}

void
player_sv_playback_speed (player_t *player, float value)
{
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
player_sv_audio_volume_get (player_t *player)
{
  int res = -1;

  if (!player)
    return -1;

  /* player specific audio_get_volume() */
  if (player->funcs->audio_get_volume)
    res = player->funcs->audio_get_volume (player);

  return res;
}

void
player_sv_audio_volume_set (player_t *player, int value)
{
  if (!player)
    return;

  /* player specific audio_set_volume() */
  if (player->funcs->audio_set_volume)
    player->funcs->audio_set_volume (player, value);
}

player_mute_t
player_sv_audio_mute_get (player_t *player)
{
  player_mute_t res = PLAYER_MUTE_UNKNOWN;

  if (!player)
    return res;

  /* player specific audio_get_mute() */
  if (player->funcs->audio_get_mute)
    res = player->funcs->audio_get_mute (player);

  return res;
}

void
player_sv_audio_mute_set (player_t *player, player_mute_t value)
{
  if (!player)
    return;

  /* player specific audio_set_mute() */
  if (player->funcs->audio_set_mute)
    player->funcs->audio_set_mute (player, value);
}

void
player_sv_audio_set_delay (player_t *player, int value, int absolute)
{
  if (!player)
    return;

  /* player specific audio_set_delay() */
  if (player->funcs->audio_set_delay)
    player->funcs->audio_set_delay (player, value, absolute);
}

void
player_sv_audio_select (player_t *player, int audio_id)
{
  if (!player)
    return;

  /* player specific audio_select() */
  if (player->funcs->audio_select)
    player->funcs->audio_select (player, audio_id);
}

void
player_sv_audio_prev (player_t *player)
{
  if (!player)
    return;

  /* player specific audio_prev() */
  if (player->funcs->audio_prev)
    player->funcs->audio_prev (player);
}

void
player_sv_audio_next (player_t *player)
{
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
player_sv_video_set_fullscreen (player_t *player, int value)
{
  if (!player)
    return;

  /* player specific video_set_fs() */
  if (player->funcs->video_set_fs)
    player->funcs->video_set_fs (player, value);
}

void
player_sv_video_set_aspect (player_t *player, player_video_aspect_t aspect,
                            int8_t value, int absolute)
{
  if (!player)
    return;

  /* player specific video_set_aspect() */
  if (player->funcs->video_set_aspect)
    player->funcs->video_set_aspect (player, aspect, value, absolute);
}

void
player_sv_video_set_panscan (player_t *player, int8_t value, int absolute)
{
  if (!player)
    return;

  /* player specific video_set_panscan() */
  if (player->funcs->video_set_panscan)
    player->funcs->video_set_panscan (player, value, absolute);
}

void
player_sv_video_set_aspect_ratio (player_t *player, float value)
{
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
player_sv_subtitle_set_delay (player_t *player, int value)
{
  if (!player)
    return;

  /* player specific sub_set_delay() */
  if (player->funcs->sub_set_delay)
    player->funcs->sub_set_delay (player, value);
}

void
player_sv_subtitle_set_alignment (player_t *player, player_sub_alignment_t a)
{
  if (!player)
    return;

  /* player specific sub_set_alignment() */
  if (player->funcs->sub_set_alignment)
    player->funcs->sub_set_alignment (player, a);
}

void
player_sv_subtitle_set_position (player_t *player, int value)
{
  if (!player)
    return;

  /* player specific sub_set_pos() */
  if (player->funcs->sub_set_pos)
    player->funcs->sub_set_pos (player, value);
}

void
player_sv_subtitle_set_visibility (player_t *player, int value)
{
  if (!player)
    return;

  /* player specific sub_set_visibility() */
  if (player->funcs->sub_set_visibility)
    player->funcs->sub_set_visibility (player, value);
}

void
player_sv_subtitle_scale (player_t *player, int value, int absolute)
{
  if (!player)
    return;

  /* player specific sub_scale() */
  if (player->funcs->sub_scale)
    player->funcs->sub_scale (player, value, absolute);
}

void
player_sv_subtitle_select (player_t *player, int sub_id)
{
  if (!player)
    return;

  /* player specific sub_select() */
  if (player->funcs->sub_select)
    player->funcs->sub_select (player, sub_id);
}

void
player_sv_subtitle_prev (player_t *player)
{
  if (!player)
    return;

  /* player specific sub_prev() */
  if (player->funcs->sub_prev)
    player->funcs->sub_prev (player);
}

void
player_sv_subtitle_next (player_t *player)
{
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
player_sv_dvd_nav (player_t *player, player_dvdnav_t value)
{
  mrl_resource_t res;

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific playback_dvdnav() */
  if (player->funcs->dvd_nav)
    player->funcs->dvd_nav (player, value);
}

void
player_sv_dvd_angle_select (player_t *player, int angle)
{
  mrl_resource_t res;

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_angle_set() */
  if (player->funcs->dvd_angle_set)
    player->funcs->dvd_angle_set (player, angle);
}

void
player_sv_dvd_angle_prev (player_t *player)
{
  mrl_resource_t res;

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_angle_prev() */
  if (player->funcs->dvd_angle_prev)
    player->funcs->dvd_angle_prev (player);
}

void
player_sv_dvd_angle_next (player_t *player)
{
  mrl_resource_t res;

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_angle_next() */
  if (player->funcs->dvd_angle_next)
    player->funcs->dvd_angle_next (player);
}

void
player_sv_dvd_title_select (player_t *player, int title)
{
  mrl_resource_t res;

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_title_set() */
  if (player->funcs->dvd_title_set)
    player->funcs->dvd_title_set (player, title);
}

void
player_sv_dvd_title_prev (player_t *player)
{
  mrl_resource_t res;

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_title_prev() */
  if (player->funcs->dvd_title_prev)
    player->funcs->dvd_title_prev (player);
}

void
player_sv_dvd_title_next (player_t *player)
{
  mrl_resource_t res;

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
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
player_sv_tv_channel_select (player_t *player, int channel)
{
  mrl_resource_t res;

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVB && res != MRL_RESOURCE_TV)
    return;

  /* player specific tv_channel_set() */
  if (player->funcs->tv_channel_set)
    player->funcs->tv_channel_set (player, channel);
}

void
player_sv_tv_channel_prev (player_t *player)
{
  mrl_resource_t res;

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVB && res != MRL_RESOURCE_TV)
    return;

  /* player specific tv_channel_prev() */
  if (player->funcs->tv_channel_prev)
    player->funcs->tv_channel_prev (player);
}

void
player_sv_tv_channel_next (player_t *player)
{
  mrl_resource_t res;

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
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
player_sv_radio_channel_select (player_t *player, int channel)
{
  if (!player)
    return;

  if (mrl_sv_get_resource (player, NULL) != MRL_RESOURCE_RADIO)
    return;

  /* player specific radio_channel_set() */
  if (player->funcs->radio_channel_set)
    player->funcs->radio_channel_set (player, channel);
}

void
player_sv_radio_channel_prev (player_t *player)
{
  if (!player)
    return;

  if (mrl_sv_get_resource (player, NULL) != MRL_RESOURCE_RADIO)
    return;

  /* player specific radio_channel_prev() */
  if (player->funcs->radio_channel_prev)
    player->funcs->radio_channel_prev (player);
}

void
player_sv_radio_channel_next (player_t *player)
{
  if (!player)
    return;

  if (mrl_sv_get_resource (player, NULL) != MRL_RESOURCE_RADIO)
    return;

  /* player specific radio_channel_next() */
  if (player->funcs->radio_channel_next)
    player->funcs->radio_channel_next (player);
}
