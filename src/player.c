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

player_t *
player_init (player_type_t type, player_ao_t ao, player_vo_t vo,
             int event_cb (player_event_t e, void *data))
{
  player_t *player = NULL;
  int res = PLAYER_INIT_ERROR;

  player = malloc (sizeof (player_t));
  player->type = type;
  player->verbosity = PLAYER_MSG_ERROR;
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

  if (!player->funcs || !player->priv)
  {
    player_uninit (player);
    return NULL;
  }

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
  if (!player)
    return;

  player->verbosity = level;

  /* player specific verbosity level */
  if (player->funcs->set_verbosity)
    player->funcs->set_verbosity (player, level);
}

void
player_playback_start (player_t *player)
{
  mrl_t *mrl;
  mrl_properties_video_t *video;
  int res = PLAYER_PB_ERROR;

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
    player->aspect = video->aspect;
  }

  /* player specific playback_start() */
  if (player->funcs->pb_start)
    res = player->funcs->pb_start (player);

  if (res != PLAYER_PB_OK)
    return;

  player->state = PLAYER_STATE_RUNNING;
  plog (player, PLAYER_MSG_INFO,
        MODULE_NAME, "Now playing: %s", player->mrl->name);

  /* notify front-end */
  if (player->event_cb)
    player->event_cb (PLAYER_EVENT_PLAYBACK_START, NULL);
}

void
player_playback_stop (player_t *player)
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
  if (player->event_cb)
    player->event_cb (PLAYER_EVENT_PLAYBACK_STOP, NULL);
}

void
player_playback_pause (player_t *player)
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
player_playback_seek (player_t *player, int value)
{
  if (!player)
    return;

  /* player specific playback_seek() */
  if (player->funcs->pb_seek)
    player->funcs->pb_seek (player, value);
}

void
player_playback_dvdnav (player_t *player, player_dvdnav_t value)
{
  if (!player)
    return;

  /* player specific playback_dvdnav() */
  if (player->funcs->pb_dvdnav)
    player->funcs->pb_dvdnav (player, value);
}

/* get player playback properties */
int
player_get_volume (player_t *player)
{
  int res = -1;

  if (!player)
    return -1;

  /* player specific get_volume() */
  if (player->funcs->get_volume)
    res = player->funcs->get_volume (player);

  return res;
}

player_mute_t
player_get_mute (player_t *player)
{
  player_mute_t res = PLAYER_MUTE_UNKNOWN;

  if (!player)
    return res;

  /* player specific get_mute() */
  if (player->funcs->get_mute)
    res = player->funcs->get_mute (player);

  return res;
}

/* tune player playback properties */
void
player_set_loop (player_t *player, int value)
{
  if (!player)
    return;

  player->loop = value;
}

void
player_set_shuffle (player_t *player, int value)
{
  if (!player)
    return;

  player->shuffle = value;
}

void
player_set_volume (player_t *player, int value)
{
  if (!player)
    return;

  /* player specific set_volume() */
  if (player->funcs->set_volume)
    player->funcs->set_volume (player, value);
}

void
player_set_mute (player_t *player, player_mute_t value)
{
  if (!player)
    return;

  /* player specific set_mute() */
  if (player->funcs->set_mute)
    player->funcs->set_mute (player, value);
}

void
player_set_sub_delay (player_t *player, float value)
{
  if (!player)
    return;

  /* player specific set_sub_delay() */
  if (player->funcs->set_sub_delay)
    player->funcs->set_sub_delay (player, value);
}
