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

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "playlist.h" /* pl_playlist_new pl_playlist_free */
#include "supervisor.h"
#include "event_handler.h"
#include "window.h"

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

static int
player_event_cb (void *data, int e)
{
  int res = 0;
  player_t *player = data;
  player_pb_t pb_mode = PLAYER_PB_SINGLE;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "internal event: %i", e);

  if (!player)
  {
    pl_event_handler_sync_release (player->event);
    return -1;
  }

  /* send to the frontend event callback */
  if (player->event_cb)
  {
    pl_supervisor_callback_in (player, pthread_self ());
    res = player->event_cb (e, player->user_data);
    pl_supervisor_callback_out (player);
  }

  if (e == PLAYER_EVENT_PLAYBACK_FINISHED)
  {
    player->state = PLAYER_STATE_IDLE;
    pb_mode = player->pb_mode;
  }

  /* release for supervisor */
  pl_event_handler_sync_release (player->event);

  /* go to the next MRL */
  if (pb_mode == PLAYER_PB_AUTO)
    pl_supervisor_send (player, SV_MODE_NO_WAIT,
                        SV_FUNC_PLAYER_MRL_NEXT_PLAY, NULL, NULL);

  return res;
}

unsigned int
libplayer_version (void)
{
  return LIBPLAYER_VERSION_INT;
}

/***************************************************************************/
/*                                                                         */
/* Player (Un)Initialization                                               */
/*                                                                         */
/***************************************************************************/

player_t *
player_init (player_type_t type,
             player_verbosity_level_t verbosity, player_init_param_t *param)
{
  player_t *player = NULL;
  init_status_t res = PLAYER_INIT_ERROR;
  supervisor_status_t sv_res;
  int ret;

  int *sv_run;
  pthread_t *sv_job;
  pthread_cond_t *sv_cond;
  pthread_mutex_t *sv_mutex;

  player = PCALLOC (player_t, 1);
  if (!player)
    return NULL;

  player->type        = type;
  player->verbosity   = verbosity;
  player->state       = PLAYER_STATE_IDLE;
  player->playlist    = pl_playlist_new (0, 0, PLAYER_LOOP_DISABLE);

  if (param)
  {
    player->ao          = param->ao;
    player->vo          = param->vo;
    player->winid       = param->winid;
    player->x11_display = param->display;
    player->event_cb    = param->event_cb;
    player->user_data   = param->data;
    player->quality     = param->quality;
  }

  pthread_mutex_init (&player->mutex_verb, NULL);

  switch (player->type)
  {
#ifdef HAVE_XINE
  case PLAYER_TYPE_XINE:
    player->funcs = pl_register_functions_xine ();
    player->priv  = pl_register_private_xine ();
    break;
#endif /* HAVE_XINE */
#ifdef HAVE_MPLAYER
  case PLAYER_TYPE_MPLAYER:
    player->funcs = pl_register_functions_mplayer ();
    player->priv  = pl_register_private_mplayer ();
    break;
#endif /* HAVE_MPLAYER */
#ifdef HAVE_VLC
  case PLAYER_TYPE_VLC:
    player->funcs = pl_register_functions_vlc ();
    player->priv  = pl_register_private_vlc ();
    break;
#endif /* HAVE_VLC */
#ifdef HAVE_GSTREAMER
  case PLAYER_TYPE_GSTREAMER:
    player->funcs = pl_register_functions_gstreamer ();
    player->priv  = pl_register_private_gstreamer ();
    break;
#endif /* HAVE_GSTREAMER */
  case PLAYER_TYPE_DUMMY:
    player->funcs = pl_register_functions_dummy ();
    player->priv  = pl_register_private_dummy ();
    break;
  default:
    break;
  }

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player->funcs || !player->priv)
  {
    pl_log (player, PLAYER_MSG_ERROR, MODULE_NAME, "no wrapper registered");
    player_uninit (player);
    return NULL;
  }

  player->supervisor = pl_supervisor_new ();
  if (!player->supervisor)
  {
    player_uninit (player);
    return NULL;
  }

  sv_res = pl_supervisor_init (player, &sv_run, &sv_job, &sv_cond, &sv_mutex);
  if (sv_res != SUPERVISOR_STATUS_OK)
  {
    pl_log (player, PLAYER_MSG_ERROR,
            MODULE_NAME, "failed to init supervisor");
    player_uninit (player);
    return NULL;
  }

  player->event = pl_event_handler_register (player, player_event_cb);
  if (!player->event)
  {
    player_uninit (player);
    return NULL;
  }

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "pl_event_handler_init");
  ret = pl_event_handler_init (player->event,
                               sv_run, sv_job, sv_cond, sv_mutex);
  if (ret)
  {
    pl_log (player, PLAYER_MSG_ERROR,
            MODULE_NAME, "failed to init event handler");
    player_uninit (player);
    return NULL;
  }

  player->window = pl_window_register (player);

  pl_event_handler_enable (player->event);

  player_set_verbosity (player, verbosity);

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_INIT, NULL, &res);
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
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_UNINIT, NULL, NULL);

  pl_window_destroy (player->window);

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "pl_event_handler_uninit");
  pl_event_handler_disable (player->event);
  pl_event_handler_uninit (player->event);

  pl_supervisor_uninit (player);

  pl_playlist_free (player->playlist);
  pthread_mutex_destroy (&player->mutex_verb);
  PFREE (player->funcs);
  PFREE (player);
}

void
player_set_verbosity (player_t *player, player_verbosity_level_t level)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_SET_VERBOSITY, &level, NULL);
}

/***************************************************************************/
/*                                                                         */
/* Player to MRL connection                                                */
/*                                                                         */
/***************************************************************************/

mrl_t *
player_mrl_get_current (player_t *player)
{
  mrl_t *out = NULL;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return NULL;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_MRL_GET_CURRENT, NULL, &out);

  return out;
}

void
player_mrl_set (player_t *player, mrl_t *mrl)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player || !mrl)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_MRL_SET, mrl, NULL);
}

void
player_mrl_append (player_t *player, mrl_t *mrl, player_mrl_add_t when)
{
  supervisor_data_mrl_t in;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player || !mrl)
    return;

  in.mrl   = mrl;
  in.value = when;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_MRL_APPEND, &in, NULL);
}

void
player_mrl_remove (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_MRL_REMOVE, NULL, NULL);
}

void
player_mrl_remove_all (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_MRL_REMOVE_ALL, NULL, NULL);
}

void
player_mrl_previous (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_MRL_PREVIOUS, NULL, NULL);
}

void
player_mrl_next (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_MRL_NEXT, NULL, NULL);
}

void
player_mrl_continue (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_MRL_NEXT_PLAY, NULL, NULL);
}

/***************************************************************************/
/*                                                                         */
/* Player tuning & properties                                              */
/*                                                                         */
/***************************************************************************/

int
player_get_time_pos (player_t *player)
{
  int out = -1;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return -1;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_GET_TIME_POS, NULL, &out);

  return out;
}

int
player_get_percent_pos (player_t *player)
{
  int out = -1;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return -1;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_GET_PERCENT_POS, NULL, &out);

  return out;
}

void
player_set_playback (player_t *player, player_pb_t pb)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_SET_PLAYBACK, &pb, NULL);
}

void
player_set_loop (player_t *player, player_loop_t loop, int value)
{
  supervisor_data_mode_t in;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  in.value = value;
  in.mode  = loop;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_SET_LOOP, &in, NULL);
}

void
player_set_shuffle (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_SET_SHUFFLE, &value, NULL);
}

void
player_set_framedrop (player_t *player, player_framedrop_t fd)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_SET_FRAMEDROP, &fd, NULL);
}

void
player_set_mouse_position (player_t *player, int x, int y)
{
  supervisor_data_coord_t in;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  in.x = x;
  in.y = y;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_SET_MOUSE_POS, &in, NULL);
}

void
player_x_window_set_properties (player_t *player,
                                int x, int y, int w, int h, int flags)
{
  supervisor_data_window_t in;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  in.x     = x;
  in.y     = y;
  in.h     = h;
  in.w     = w;
  in.flags = flags;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_X_WINDOW_SET_PROPS, &in, NULL);
}

void
player_osd_show_text (player_t *player,
                      const char *text, int x, int y, int duration)
{
  supervisor_data_osd_t in;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  in.text     = text;
  in.x        = x;
  in.y        = y;
  in.duration = duration;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_OSD_SHOW_TEXT, &in, NULL);
}

void
player_osd_state (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_OSD_STATE, &value, NULL);
}

/***************************************************************************/
/*                                                                         */
/* Playback related controls                                               */
/*                                                                         */
/***************************************************************************/

player_pb_state_t
player_playback_get_state (player_t *player)
{
  player_pb_state_t out = PLAYER_PB_STATE_IDLE;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return out;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_PB_GET_STATE, NULL, &out);

  return out;
}

void
player_playback_start (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_PB_START, NULL, NULL);
}

void
player_playback_stop (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_PB_STOP, NULL, NULL);
}

void
player_playback_pause (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_PB_PAUSE, NULL, NULL);
}

void
player_playback_seek (player_t *player, int value, player_pb_seek_t seek)
{
  supervisor_data_mode_t in;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  in.value = value;
  in.mode  = seek;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_PB_SEEK, &in, NULL);
}

void
player_playback_seek_chapter (player_t *player, int value, int absolute)
{
  supervisor_data_mode_t in;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  in.value = value;
  in.mode  = absolute;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_PB_SEEK_CHAPTER, &in, NULL);
}

void
player_playback_speed (player_t *player, float value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_PB_SPEED, &value, NULL);
}

/***************************************************************************/
/*                                                                         */
/* Audio related controls                                                  */
/*                                                                         */
/***************************************************************************/

int
player_audio_volume_get (player_t *player)
{
  int out = -1;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return -1;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_AO_VOLUME_GET, NULL, &out);

  return out;
}

void
player_audio_volume_set (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_AO_VOLUME_SET, &value, NULL);
}

player_mute_t
player_audio_mute_get (player_t *player)
{
  player_mute_t out = PLAYER_MUTE_UNKNOWN;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return out;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_AO_MUTE_GET, NULL, &out);

  return out;
}

void
player_audio_mute_set (player_t *player, player_mute_t value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_AO_MUTE_SET, &value, NULL);
}

void
player_audio_set_delay (player_t *player, int value, int absolute)
{
  supervisor_data_mode_t in;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  in.value = value;
  in.mode  = absolute;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_AO_SET_DELAY, &in, NULL);
}

void
player_audio_select (player_t *player, int audio_id)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_AO_SELECT, &audio_id, NULL);
}

void
player_audio_prev (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_AO_PREV, NULL, NULL);
}

void
player_audio_next (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_AO_NEXT, NULL, NULL);
}

/***************************************************************************/
/*                                                                         */
/* Video related controls                                                  */
/*                                                                         */
/***************************************************************************/

void
player_video_set_aspect (player_t *player, player_video_aspect_t aspect,
                         int8_t value, int absolute)
{
  supervisor_data_vo_t in;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  in.list  = aspect;
  in.value = value;
  in.mode  = absolute;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_VO_SET_ASPECT, &in, NULL);
}

void
player_video_set_panscan (player_t *player, int8_t value, int absolute)
{
  supervisor_data_vo_t in;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  in.list  = 0;
  in.value = value;
  in.mode  = absolute;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_VO_SET_PANSCAN, &in, NULL);
}

void
player_video_set_aspect_ratio (player_t *player, float value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_VO_SET_AR, &value, NULL);
}

/***************************************************************************/
/*                                                                         */
/* Subtitles related controls                                              */
/*                                                                         */
/***************************************************************************/

void
player_subtitle_set_delay (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_SUB_SET_DELAY, &value, NULL);
}

void
player_subtitle_set_alignment (player_t *player,
                               player_sub_alignment_t a)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_SUB_SET_ALIGN, &a, NULL);
}

void
player_subtitle_set_position (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_SUB_SET_POS, &value, NULL);
}

void
player_subtitle_set_visibility (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_SUB_SET_VIS, &value, NULL);
}

void
player_subtitle_scale (player_t *player, int value, int absolute)
{
  supervisor_data_mode_t in;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  in.value = value;
  in.mode  = absolute;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_SUB_SCALE, &in, NULL);
}

void
player_subtitle_select (player_t *player, int sub_id)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_SUB_SELECT, &sub_id, NULL);
}

void
player_subtitle_prev (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_SUB_PREV, NULL, NULL);
}

void
player_subtitle_next (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_SUB_NEXT, NULL, NULL);
}

/***************************************************************************/
/*                                                                         */
/* DVD specific controls                                                   */
/*                                                                         */
/***************************************************************************/

void
player_dvd_nav (player_t *player, player_dvdnav_t value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_DVD_NAV, &value, NULL);
}

void
player_dvd_angle_select (player_t *player, int angle)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_DVD_ANGLE_SELECT, &angle, NULL);
}

void
player_dvd_angle_prev (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_DVD_ANGLE_PREV, NULL, NULL);
}

void
player_dvd_angle_next (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_DVD_ANGLE_NEXT, NULL, NULL);
}

void
player_dvd_title_select (player_t *player, int title)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_DVD_ANGLE_SELECT, &title, NULL);
}

void
player_dvd_title_prev (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_DVD_TITLE_PREV, NULL, NULL);
}

void
player_dvd_title_next (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_DVD_TITLE_NEXT, NULL, NULL);
}

/***************************************************************************/
/*                                                                         */
/* TV/DVB specific controls                                                */
/*                                                                         */
/***************************************************************************/

void
player_tv_channel_select (player_t *player, const char *channel)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_TV_CHAN_SELECT, (void *) channel, NULL);
}

void
player_tv_channel_prev (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_TV_CHAN_PREV, NULL, NULL);
}

void
player_tv_channel_next (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_TV_CHAN_NEXT, NULL, NULL);
}

/***************************************************************************/
/*                                                                         */
/* Radio specific controls                                                 */
/*                                                                         */
/***************************************************************************/

void
player_radio_channel_select (player_t *player, const char *channel)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_RADIO_CHAN_SELECT, (void *) channel, NULL);
}

void
player_radio_channel_prev (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_RADIO_CHAN_PREV, NULL, NULL);
}

void
player_radio_channel_next (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_RADIO_CHAN_NEXT, NULL, NULL);
}

/***************************************************************************/
/*                                                                         */
/* VDR specific controls                                                   */
/*                                                                         */
/***************************************************************************/

void
player_vdr (player_t *player, player_vdr_t value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  pl_supervisor_send (player, SV_MODE_WAIT_FOR_END,
                      SV_FUNC_PLAYER_VDR, &value, NULL);
}

/***************************************************************************/
/*                                                                         */
/* Global libplayer functions                                              */
/*                                                                         */
/***************************************************************************/

int
libplayer_wrapper_enabled (player_type_t type)
{
  switch (type)
  {
#ifdef HAVE_XINE
  case PLAYER_TYPE_XINE:
#endif /* HAVE_XINE */
#ifdef HAVE_MPLAYER
  case PLAYER_TYPE_MPLAYER:
#endif /* HAVE_MPLAYER */
#ifdef HAVE_VLC
  case PLAYER_TYPE_VLC:
#endif /* HAVE_VLC */
#ifdef HAVE_GSTREAMER
  case PLAYER_TYPE_GSTREAMER:
#endif /* HAVE_GSTREAMER */
  case PLAYER_TYPE_DUMMY:
    return 1;

  default:
    return 0;
  }
}

int
libplayer_wrapper_supported_res (player_type_t type, mrl_resource_t res)
{
  switch (type)
  {
#ifdef HAVE_XINE
  case PLAYER_TYPE_XINE:
    return pl_supported_resources_xine (res);
#endif /* HAVE_XINE */

#ifdef HAVE_MPLAYER
  case PLAYER_TYPE_MPLAYER:
    return pl_supported_resources_mplayer (res);
#endif /* HAVE_MPLAYER */

#ifdef HAVE_VLC
  case PLAYER_TYPE_VLC:
    return pl_supported_resources_vlc (res);
#endif /* HAVE_VLC */

#ifdef HAVE_GSTREAMER
  case PLAYER_TYPE_GSTREAMER:
    return pl_supported_resources_gstreamer (res);
#endif /* HAVE_GSTREAMER */

  case PLAYER_TYPE_DUMMY:
    return pl_supported_resources_dummy (res);

  default:
    return 0;
  }
}
