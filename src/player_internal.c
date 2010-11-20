/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006-2008 Benjamin Zores <ben@geexbox.org>
 * Copyright (C) 2007-2008 Mathieu Schroeter <mathieu@schroetersa.ch>
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
#include <inttypes.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "playlist.h"
#include "event.h"
#include "window.h"

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

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return res;

  /* player specific init */
  PLAYER_FUNCS_RES (init, res)

  return res;
}

void
player_sv_uninit (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* free player specific private properties */
  PLAYER_FUNCS (uninit)
}

void
player_sv_set_verbosity (player_t *player, player_verbosity_level_t level)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pthread_mutex_lock (&player->mutex_verb);
  player->verbosity = level;
  pthread_mutex_unlock (&player->mutex_verb);

  /* player specific verbosity level */
  PLAYER_FUNCS (set_verbosity, level)
}

/***************************************************************************/
/*                                                                         */
/* Player to MRL connection                                                */
/*                                                                         */
/***************************************************************************/

mrl_t *
player_sv_mrl_get_current (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return NULL;

  return pl_playlist_get_mrl (player->playlist);
}

void
player_sv_mrl_set (player_t *player, mrl_t *mrl)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player || !mrl)
    return;

  pl_playlist_set_mrl (player->playlist, mrl);
}

void
player_sv_mrl_append (player_t *player, mrl_t *mrl, player_mrl_add_t when)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player || !mrl)
    return;

  pl_playlist_append_mrl (player->playlist, mrl);

  /* play it now ? */
  if (when == PLAYER_MRL_ADD_NOW)
  {
    player_sv_playback_stop (player);
    pl_playlist_last_mrl (player->playlist);
    player_sv_playback_start (player);
  }
}

void
player_sv_mrl_remove (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_playlist_remove_mrl (player->playlist);
}

void
player_sv_mrl_remove_all (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  player_sv_playback_stop (player);
  pl_playlist_empty (player->playlist);
}

void
player_sv_mrl_previous (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (!pl_playlist_previous_mrl_available (player->playlist))
    return;

  player_sv_playback_stop (player);
  pl_playlist_previous_mrl (player->playlist);
  player_sv_playback_start (player);
}

void
player_sv_mrl_next (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (!pl_playlist_next_mrl_available (player->playlist))
    return;

  player_sv_playback_stop (player);
  pl_playlist_next_mrl (player->playlist);
  player_sv_playback_start (player);
}

void
player_sv_mrl_next_play (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (player->pb_mode != PLAYER_PB_AUTO)
  {
    player_sv_mrl_next (player);
    return;
  }

  player_sv_playback_stop (player);

  if (!pl_playlist_next_play (player->playlist))
  {
    player_event_send (player, PLAYER_EVENT_PLAYLIST_FINISHED);
    return;
  }

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

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return -1;

  /* player specific get_time_pos() */
  PLAYER_FUNCS_RES (get_time_pos, res)

  return res;
}

int
player_sv_get_percent_pos (player_t *player)
{
  int res = -1;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return -1;

  /* player specific get_percent_pos() */
  PLAYER_FUNCS_RES (get_percent_pos, res)

  return res;
}

void
player_sv_set_playback (player_t *player, player_pb_t pb)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  player->pb_mode = pb;
}

void
player_sv_set_loop (player_t *player, player_loop_t loop, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (player->pb_mode != PLAYER_PB_AUTO && loop != PLAYER_LOOP_DISABLE)
    pl_log (player, PLAYER_MSG_WARNING,
            MODULE_NAME, "loop is only activated with PLAYBACK_AUTO mode");

  pl_playlist_set_loop (player->playlist, value, loop);
}

void
player_sv_set_shuffle (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (player->pb_mode != PLAYER_PB_AUTO && value)
    pl_log (player, PLAYER_MSG_WARNING,
            MODULE_NAME, "shuffle is only activated with PLAYBACK_AUTO mode");

  pl_playlist_set_shuffle (player->playlist, value);
}

void
player_sv_set_framedrop (player_t *player, player_framedrop_t fd)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific set_framedrop() */
  PLAYER_FUNCS (set_framedrop, fd)
}

void
player_sv_set_mouse_position (player_t *player, int x, int y)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific set_mouse_pos() */
  PLAYER_FUNCS (set_mouse_pos, x, y)
}

void
player_sv_x_window_set_properties (player_t *player,
                                   int x, int y, int w, int h, int flags)
{
  int f = 0;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (!player->window)
  {
    pl_log (player, PLAYER_MSG_VERBOSE,
            MODULE_NAME, "ignored because no X window");
    return;
  }

  if (flags == PLAYER_X_WINDOW_AUTO)
  {
    f = WIN_PROPERTY_X | WIN_PROPERTY_Y |
        WIN_PROPERTY_W | WIN_PROPERTY_H;
    x = 0;
    y = 0;
    w = 0;
    h = 0;
  }
  else
  {
    if (flags & PLAYER_X_WINDOW_X)
      f = WIN_PROPERTY_X;
    if (flags & PLAYER_X_WINDOW_Y)
      f |= WIN_PROPERTY_Y;
    if (flags & PLAYER_X_WINDOW_W)
      f |= WIN_PROPERTY_W;
    if (flags & PLAYER_X_WINDOW_H)
      f |= WIN_PROPERTY_H;
  }

  pl_window_win_props_set (player->window, x, y, w, h, f);
  pl_window_resize (player->window);
}

void
player_sv_osd_show_text (player_t *player,
                         const char *text, int x, int y, int duration)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific osd_show_text() */
  PLAYER_FUNCS (osd_show_text, text, x, y, duration)
}

void
player_sv_osd_state (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific osd_state() */
  PLAYER_FUNCS (osd_state, value)
}

/***************************************************************************/
/*                                                                         */
/* Playback related controls                                               */
/*                                                                         */
/***************************************************************************/

player_pb_state_t
player_sv_playback_get_state (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return PLAYER_PB_STATE_IDLE;

  switch (player->state)
  {
  default:
  case PLAYER_STATE_IDLE:
    return PLAYER_PB_STATE_IDLE;

  case PLAYER_STATE_PAUSE:
    return PLAYER_PB_STATE_PAUSE;

  case PLAYER_STATE_RUNNING:
    return PLAYER_PB_STATE_PLAY;
  }
}

void
player_sv_playback_start (player_t *player)
{
  mrl_t *mrl;
  int res = PLAYER_PB_ERROR;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (player->state != PLAYER_STATE_IDLE) /* already running : stop it */
    player_sv_playback_stop (player);

  mrl = pl_playlist_get_mrl (player->playlist);
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
  PLAYER_FUNCS_RES (pb_start, res)

  if (res != PLAYER_PB_OK)
    return;

  player->state = PLAYER_STATE_RUNNING;

  /* notify front-end */
  player_event_send (player, PLAYER_EVENT_PLAYBACK_START);
}

void
player_sv_playback_stop (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (player->state == PLAYER_STATE_IDLE)
    return; /* not running */

  /* player specific playback_stop() */
  PLAYER_FUNCS (pb_stop)

  player->state = PLAYER_STATE_IDLE;

  /* notify front-end */
  player_event_send (player, PLAYER_EVENT_PLAYBACK_STOP);
}

void
player_sv_playback_pause (player_t *player)
{
  int res = PLAYER_PB_ERROR;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (player->state != PLAYER_STATE_PAUSE
      && player->state != PLAYER_STATE_RUNNING)
    return;

  /* player specific playback_pause() */
  PLAYER_FUNCS_RES (pb_pause, res)

  if (res != PLAYER_PB_OK)
    return;

  if (player->state == PLAYER_STATE_RUNNING)
  {
    player->state = PLAYER_STATE_PAUSE;
    player_event_send (player, PLAYER_EVENT_PLAYBACK_PAUSE);
  }
  else
  {
    player->state = PLAYER_STATE_RUNNING;
    player_event_send (player, PLAYER_EVENT_PLAYBACK_UNPAUSE);
  }
}

void
player_sv_playback_seek (player_t *player, int value, player_pb_seek_t seek)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific playback_seek() */
  PLAYER_FUNCS (pb_seek, value, seek)
}

void
player_sv_playback_seek_chapter (player_t *player, int value, int absolute)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific playback_seek_chapter() */
  PLAYER_FUNCS (pb_seek_chapter, value, absolute)
}

void
player_sv_playback_speed (player_t *player, float value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific playback_set_speed() */
  PLAYER_FUNCS (pb_set_speed, value)
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

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return -1;

  /* player specific audio_get_volume() */
  PLAYER_FUNCS_RES (audio_get_volume, res)

  return res;
}

void
player_sv_audio_volume_set (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_set_volume() */
  PLAYER_FUNCS (audio_set_volume, value)
}

player_mute_t
player_sv_audio_mute_get (player_t *player)
{
  player_mute_t res = PLAYER_MUTE_UNKNOWN;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return res;

  /* player specific audio_get_mute() */
  PLAYER_FUNCS_RES (audio_get_mute, res)

  return res;
}

void
player_sv_audio_mute_set (player_t *player, player_mute_t value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_set_mute() */
  PLAYER_FUNCS (audio_set_mute, value)
}

void
player_sv_audio_set_delay (player_t *player, int value, int absolute)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_set_delay() */
  PLAYER_FUNCS (audio_set_delay, value, absolute)
}

void
player_sv_audio_select (player_t *player, int audio_id)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_select() */
  PLAYER_FUNCS (audio_select, audio_id)
}

void
player_sv_audio_prev (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_prev() */
  PLAYER_FUNCS (audio_prev)
}

void
player_sv_audio_next (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_next() */
  PLAYER_FUNCS (audio_next)
}

/***************************************************************************/
/*                                                                         */
/* Video related controls                                                  */
/*                                                                         */
/***************************************************************************/

void
player_sv_video_set_aspect (player_t *player, player_video_aspect_t aspect,
                            int8_t value, int absolute)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific video_set_aspect() */
  PLAYER_FUNCS (video_set_aspect, aspect, value, absolute)
}

void
player_sv_video_set_panscan (player_t *player, int8_t value, int absolute)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific video_set_panscan() */
  PLAYER_FUNCS (video_set_panscan, value, absolute)
}

void
player_sv_video_set_aspect_ratio (player_t *player, float value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific video_set_ar() */
  PLAYER_FUNCS (video_set_ar, value)
}

/***************************************************************************/
/*                                                                         */
/* Subtitles related controls                                              */
/*                                                                         */
/***************************************************************************/

void
player_sv_subtitle_set_delay (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_set_delay() */
  PLAYER_FUNCS (sub_set_delay, value)
}

void
player_sv_subtitle_set_alignment (player_t *player, player_sub_alignment_t a)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_set_alignment() */
  PLAYER_FUNCS (sub_set_alignment, a)
}

void
player_sv_subtitle_set_position (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_set_pos() */
  PLAYER_FUNCS (sub_set_pos, value)
}

void
player_sv_subtitle_set_visibility (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_set_visibility() */
  PLAYER_FUNCS (sub_set_visibility, value)
}

void
player_sv_subtitle_scale (player_t *player, int value, int absolute)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_scale() */
  PLAYER_FUNCS (sub_scale, value, absolute)
}

void
player_sv_subtitle_select (player_t *player, int sub_id)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_select() */
  PLAYER_FUNCS (sub_select, sub_id)
}

void
player_sv_subtitle_prev (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_prev() */
  PLAYER_FUNCS (sub_prev)
}

void
player_sv_subtitle_next (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_next() */
  PLAYER_FUNCS (sub_next)
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

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_nav() */
  PLAYER_FUNCS (dvd_nav, value)
}

void
player_sv_dvd_angle_select (player_t *player, int angle)
{
  mrl_resource_t res;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_angle_set() */
  PLAYER_FUNCS (dvd_angle_set, angle)
}

void
player_sv_dvd_angle_prev (player_t *player)
{
  mrl_resource_t res;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_angle_prev() */
  PLAYER_FUNCS (dvd_angle_prev)
}

void
player_sv_dvd_angle_next (player_t *player)
{
  mrl_resource_t res;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_angle_next() */
  PLAYER_FUNCS (dvd_angle_next)
}

void
player_sv_dvd_title_select (player_t *player, int title)
{
  mrl_resource_t res;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_title_set() */
  PLAYER_FUNCS (dvd_title_set, title)
}

void
player_sv_dvd_title_prev (player_t *player)
{
  mrl_resource_t res;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_title_prev() */
  PLAYER_FUNCS (dvd_title_prev)
}

void
player_sv_dvd_title_next (player_t *player)
{
  mrl_resource_t res;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_title_next() */
  PLAYER_FUNCS (dvd_title_next)
}

/***************************************************************************/
/*                                                                         */
/* TV/DVB specific controls                                                */
/*                                                                         */
/***************************************************************************/

void
player_sv_tv_channel_select (player_t *player, const char *channel)
{
  mrl_resource_t res;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVB && res != MRL_RESOURCE_TV)
    return;

  /* player specific tv_channel_set() */
  PLAYER_FUNCS (tv_channel_set, channel)
}

void
player_sv_tv_channel_prev (player_t *player)
{
  mrl_resource_t res;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVB && res != MRL_RESOURCE_TV)
    return;

  /* player specific tv_channel_prev() */
  PLAYER_FUNCS (tv_channel_prev)
}

void
player_sv_tv_channel_next (player_t *player)
{
  mrl_resource_t res;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVB && res != MRL_RESOURCE_TV)
    return;

  /* player specific tv_channel_next() */
  PLAYER_FUNCS (tv_channel_next)
}

/***************************************************************************/
/*                                                                         */
/* Radio specific controls                                                 */
/*                                                                         */
/***************************************************************************/

void
player_sv_radio_channel_select (player_t *player, const char *channel)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (mrl_sv_get_resource (player, NULL) != MRL_RESOURCE_RADIO)
    return;

  /* player specific radio_channel_set() */
  PLAYER_FUNCS (radio_channel_set, channel)
}

void
player_sv_radio_channel_prev (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (mrl_sv_get_resource (player, NULL) != MRL_RESOURCE_RADIO)
    return;

  /* player specific radio_channel_prev() */
  PLAYER_FUNCS (radio_channel_prev)
}

void
player_sv_radio_channel_next (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (mrl_sv_get_resource (player, NULL) != MRL_RESOURCE_RADIO)
    return;

  /* player specific radio_channel_next() */
  PLAYER_FUNCS (radio_channel_next)
}

/***************************************************************************/
/*                                                                         */
/* VDR specific controls                                                   */
/*                                                                         */
/***************************************************************************/

void
player_sv_vdr (player_t *player, player_vdr_t value)
{
  mrl_resource_t res;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_VDR && res != MRL_RESOURCE_NETVDR)
    return;

  /* player specific vdr() */
  PLAYER_FUNCS (vdr, value)
}
