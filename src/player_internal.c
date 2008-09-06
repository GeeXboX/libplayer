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

#ifdef USE_X11
#include "x11_common.h"
#endif /* USE_X11 */

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

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return res;

  /* player specific init */
  if (player->funcs->init)
    res = player->funcs->init (player);
  else
    plog (player, PLAYER_MSG_WARNING, MODULE_NAME, "init is unimplemented");

  return res;
}

void
player_sv_uninit (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* free player specific private properties */
  if (player->funcs->uninit)
    player->funcs->uninit (player);
  else
    plog (player, PLAYER_MSG_WARNING, MODULE_NAME, "uninit is unimplemented");
}

void
player_sv_set_verbosity (player_t *player, player_verbosity_level_t level)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pthread_mutex_lock (&player->mutex_verb);
  player->verbosity = level;
  pthread_mutex_unlock (&player->mutex_verb);

  /* player specific verbosity level */
  if (player->funcs->set_verbosity)
    player->funcs->set_verbosity (player, level);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "set_verbosity is unimplemented");
}

void
player_sv_x_window_set_properties (player_t *player,
                                   int x, int y, int w, int h, int flags)
{
#ifdef USE_X11
  int f = 0;
#endif /* USE_X11 */

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player || !player->x11)
    return;

#ifdef USE_X11
  if (flags == PLAYER_X_WINDOW_AUTO)
  {
    f = PLAYER_X_WINDOW_X | PLAYER_X_WINDOW_Y |
        PLAYER_X_WINDOW_W | PLAYER_X_WINDOW_H;
    x = 0;
    y = 0;
    w = 0;
    h = 0;
  }
  else
  {
    if (flags & PLAYER_X_WINDOW_X)
      f = X11_PROPERTY_X;
    if (flags & PLAYER_X_WINDOW_Y)
      f |= X11_PROPERTY_Y;
    if (flags & PLAYER_X_WINDOW_W)
      f |= X11_PROPERTY_W;
    if (flags & PLAYER_X_WINDOW_H)
      f |= X11_PROPERTY_H;
  }

  x11_set_winprops (player->x11, x, y, w, h, f);
  x11_resize (player);
#endif /* USE_X11 */
}

/***************************************************************************/
/*                                                                         */
/* Player to MRL connection                                                */
/*                                                                         */
/***************************************************************************/

mrl_t *
player_sv_mrl_get_current (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return NULL;

  return playlist_get_mrl (player->playlist);
}

void
player_sv_mrl_set (player_t *player, mrl_t *mrl)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player || !mrl)
    return;

  playlist_set_mrl (player->playlist, mrl);
}

void
player_sv_mrl_append (player_t *player, mrl_t *mrl, player_mrl_add_t when)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

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
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  playlist_remove_mrl (player->playlist);
}

void
player_sv_mrl_remove_all (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  player_sv_playback_stop (player);
  playlist_empty (player->playlist);
}

void
player_sv_mrl_previous (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

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
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

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
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  player_sv_playback_stop (player);

  if (!playlist_next_play (player->playlist))
  {
    player_event_send (player, PLAYER_EVENT_PLAYLIST_FINISHED, NULL);
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

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return -1;

  /* player specific get_time_pos() */
  if (player->funcs->get_time_pos)
    res = player->funcs->get_time_pos (player);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "get_time_pos is unimplemented");

  return res;
}

void
player_sv_set_playback (player_t *player, player_pb_t pb)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  player->pb_mode = pb;
}

void
player_sv_set_loop (player_t *player, player_loop_t loop, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

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
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (player->pb_mode != PLAYER_PB_AUTO && value)
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "shuffle is only activated with PLAYBACK_AUTO mode");

  playlist_set_shuffle (player->playlist, value);
}

void
player_sv_set_framedrop (player_t *player, player_framedrop_t fd)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific set_framedrop() */
  if (player->funcs->set_framedrop)
    player->funcs->set_framedrop (player, fd);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "set_framedrop is unimplemented");
}

/***************************************************************************/
/*                                                                         */
/* Playback related controls                                               */
/*                                                                         */
/***************************************************************************/

player_pb_state_t
player_sv_playback_get_state (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

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

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

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
  else
    plog (player, PLAYER_MSG_WARNING, MODULE_NAME, "pb_start is unimplemented");

  if (res != PLAYER_PB_OK)
    return;

  player->state = PLAYER_STATE_RUNNING;

  /* notify front-end */
  player_event_send (player, PLAYER_EVENT_PLAYBACK_START, NULL);
}

void
player_sv_playback_stop (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (player->state == PLAYER_STATE_IDLE)
    return; /* not running */

  /* player specific playback_stop() */
  if (player->funcs->pb_stop)
    player->funcs->pb_stop (player);
  else
    plog (player, PLAYER_MSG_WARNING, MODULE_NAME, "pb_stop is unimplemented");

  player->state = PLAYER_STATE_IDLE;

  /* notify front-end */
  player_event_send (player, PLAYER_EVENT_PLAYBACK_STOP, NULL);
}

void
player_sv_playback_pause (player_t *player)
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
  else
    plog (player, PLAYER_MSG_WARNING, MODULE_NAME, "pb_pause is unimplemented");

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
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific playback_seek() */
  if (player->funcs->pb_seek)
    player->funcs->pb_seek (player, value, seek);
  else
    plog (player, PLAYER_MSG_WARNING, MODULE_NAME, "pb_seek is unimplemented");
}

void
player_sv_playback_seek_chapter (player_t *player, int value, int absolute)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific playback_seek_chapter() */
  if (player->funcs->pb_seek_chapter)
    player->funcs->pb_seek_chapter (player, value, absolute);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "pb_seek_chapter is unimplemented");
}

void
player_sv_playback_speed (player_t *player, float value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific playback_set_speed() */
  if (player->funcs->pb_set_speed)
    player->funcs->pb_set_speed (player, value);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "pb_set_speed is unimplemented");
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

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return -1;

  /* player specific audio_get_volume() */
  if (player->funcs->audio_get_volume)
    res = player->funcs->audio_get_volume (player);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "audio_get_volume is unimplemented");

  return res;
}

void
player_sv_audio_volume_set (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_set_volume() */
  if (player->funcs->audio_set_volume)
    player->funcs->audio_set_volume (player, value);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "audio_set_volume is unimplemented");
}

player_mute_t
player_sv_audio_mute_get (player_t *player)
{
  player_mute_t res = PLAYER_MUTE_UNKNOWN;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return res;

  /* player specific audio_get_mute() */
  if (player->funcs->audio_get_mute)
    res = player->funcs->audio_get_mute (player);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "audio_get_mute is unimplemented");

  return res;
}

void
player_sv_audio_mute_set (player_t *player, player_mute_t value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_set_mute() */
  if (player->funcs->audio_set_mute)
    player->funcs->audio_set_mute (player, value);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "audio_set_mute is unimplemented");
}

void
player_sv_audio_set_delay (player_t *player, int value, int absolute)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_set_delay() */
  if (player->funcs->audio_set_delay)
    player->funcs->audio_set_delay (player, value, absolute);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "audio_set_delay is unimplemented");
}

void
player_sv_audio_select (player_t *player, int audio_id)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_select() */
  if (player->funcs->audio_select)
    player->funcs->audio_select (player, audio_id);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "audio_select is unimplemented");
}

void
player_sv_audio_prev (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_prev() */
  if (player->funcs->audio_prev)
    player->funcs->audio_prev (player);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "audio_prev is unimplemented");
}

void
player_sv_audio_next (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific audio_next() */
  if (player->funcs->audio_next)
    player->funcs->audio_next (player);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "audio_next is unimplemented");
}

/***************************************************************************/
/*                                                                         */
/* Video related controls                                                  */
/*                                                                         */
/***************************************************************************/

void
player_sv_video_set_fullscreen (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific video_set_fs() */
  if (player->funcs->video_set_fs)
    player->funcs->video_set_fs (player, value);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "video_set_fs is unimplemented");
}

void
player_sv_video_set_aspect (player_t *player, player_video_aspect_t aspect,
                            int8_t value, int absolute)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific video_set_aspect() */
  if (player->funcs->video_set_aspect)
    player->funcs->video_set_aspect (player, aspect, value, absolute);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "video_set_aspect is unimplemented");
}

void
player_sv_video_set_panscan (player_t *player, int8_t value, int absolute)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific video_set_panscan() */
  if (player->funcs->video_set_panscan)
    player->funcs->video_set_panscan (player, value, absolute);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "video_set_panscan is unimplemented");
}

void
player_sv_video_set_aspect_ratio (player_t *player, float value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific video_set_ar() */
  if (player->funcs->video_set_ar)
    player->funcs->video_set_ar (player, value);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "video_set_ar is unimplemented");
}

/***************************************************************************/
/*                                                                         */
/* Subtitles related controls                                              */
/*                                                                         */
/***************************************************************************/

void
player_sv_subtitle_set_delay (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_set_delay() */
  if (player->funcs->sub_set_delay)
    player->funcs->sub_set_delay (player, value);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "sub_set_delay is unimplemented");
}

void
player_sv_subtitle_set_alignment (player_t *player, player_sub_alignment_t a)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_set_alignment() */
  if (player->funcs->sub_set_alignment)
    player->funcs->sub_set_alignment (player, a);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "sub_set_alignment is unimplemented");
}

void
player_sv_subtitle_set_position (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_set_pos() */
  if (player->funcs->sub_set_pos)
    player->funcs->sub_set_pos (player, value);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "sub_set_pos is unimplemented");
}

void
player_sv_subtitle_set_visibility (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_set_visibility() */
  if (player->funcs->sub_set_visibility)
    player->funcs->sub_set_visibility (player, value);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "sub_set_visibility is unimplemented");
}

void
player_sv_subtitle_scale (player_t *player, int value, int absolute)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_scale() */
  if (player->funcs->sub_scale)
    player->funcs->sub_scale (player, value, absolute);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "sub_scale is unimplemented");
}

void
player_sv_subtitle_select (player_t *player, int sub_id)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_select() */
  if (player->funcs->sub_select)
    player->funcs->sub_select (player, sub_id);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "sub_select is unimplemented");
}

void
player_sv_subtitle_prev (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_prev() */
  if (player->funcs->sub_prev)
    player->funcs->sub_prev (player);
  else
    plog (player, PLAYER_MSG_WARNING, MODULE_NAME, "sub_prev is unimplemented");
}

void
player_sv_subtitle_next (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  /* player specific sub_next() */
  if (player->funcs->sub_next)
    player->funcs->sub_next (player);
  else
    plog (player, PLAYER_MSG_WARNING, MODULE_NAME, "sub_next is unimplemented");
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

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific playback_dvdnav() */
  if (player->funcs->dvd_nav)
    player->funcs->dvd_nav (player, value);
  else
    plog (player, PLAYER_MSG_WARNING, MODULE_NAME, "dvd_nav is unimplemented");
}

void
player_sv_dvd_angle_select (player_t *player, int angle)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_angle_set() */
  if (player->funcs->dvd_angle_set)
    player->funcs->dvd_angle_set (player, angle);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "dvd_angle_set is unimplemented");
}

void
player_sv_dvd_angle_prev (player_t *player)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_angle_prev() */
  if (player->funcs->dvd_angle_prev)
    player->funcs->dvd_angle_prev (player);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "dvd_angle_prev is unimplemented");
}

void
player_sv_dvd_angle_next (player_t *player)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_angle_next() */
  if (player->funcs->dvd_angle_next)
    player->funcs->dvd_angle_next (player);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "dvd_angle_next is unimplemented");
}

void
player_sv_dvd_title_select (player_t *player, int title)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_title_set() */
  if (player->funcs->dvd_title_set)
    player->funcs->dvd_title_set (player, title);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "dvd_title_set is unimplemented");
}

void
player_sv_dvd_title_prev (player_t *player)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_title_prev() */
  if (player->funcs->dvd_title_prev)
    player->funcs->dvd_title_prev (player);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "dvd_title_prev is unimplemented");
}

void
player_sv_dvd_title_next (player_t *player)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVD && res != MRL_RESOURCE_DVDNAV)
    return;

  /* player specific dvd_title_next() */
  if (player->funcs->dvd_title_next)
    player->funcs->dvd_title_next (player);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "dvd_title_next is unimplemented");
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

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVB && res != MRL_RESOURCE_TV)
    return;

  /* player specific tv_channel_set() */
  if (player->funcs->tv_channel_set)
    player->funcs->tv_channel_set (player, channel);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "tv_channel_set is unimplemented");
}

void
player_sv_tv_channel_prev (player_t *player)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVB && res != MRL_RESOURCE_TV)
    return;

  /* player specific tv_channel_prev() */
  if (player->funcs->tv_channel_prev)
    player->funcs->tv_channel_prev (player);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "tv_channel_prev is unimplemented");
}

void
player_sv_tv_channel_next (player_t *player)
{
  mrl_resource_t res;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  res = mrl_sv_get_resource (player, NULL);
  if (res != MRL_RESOURCE_DVB && res != MRL_RESOURCE_TV)
    return;

  /* player specific tv_channel_next() */
  if (player->funcs->tv_channel_next)
    player->funcs->tv_channel_next (player);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "tv_channel_next is unimplemented");
}

/***************************************************************************/
/*                                                                         */
/* Radio specific controls                                                 */
/*                                                                         */
/***************************************************************************/

void
player_sv_radio_channel_select (player_t *player, int channel)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (mrl_sv_get_resource (player, NULL) != MRL_RESOURCE_RADIO)
    return;

  /* player specific radio_channel_set() */
  if (player->funcs->radio_channel_set)
    player->funcs->radio_channel_set (player, channel);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "radio_channel_set is unimplemented");
}

void
player_sv_radio_channel_prev (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (mrl_sv_get_resource (player, NULL) != MRL_RESOURCE_RADIO)
    return;

  /* player specific radio_channel_prev() */
  if (player->funcs->radio_channel_prev)
    player->funcs->radio_channel_prev (player);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "radio_channel_prev is unimplemented");
}

void
player_sv_radio_channel_next (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  if (mrl_sv_get_resource (player, NULL) != MRL_RESOURCE_RADIO)
    return;

  /* player specific radio_channel_next() */
  if (player->funcs->radio_channel_next)
    player->funcs->radio_channel_next (player);
  else
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "radio_channel_next is unimplemented");
}
