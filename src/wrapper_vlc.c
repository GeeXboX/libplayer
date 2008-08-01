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
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <vlc/vlc.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "playlist.h"
#include "wrapper_vlc.h"
#include "x11_common.h"

#define MODULE_NAME "vlc"

/* player specific structure */
typedef struct vlc_s {
  libvlc_instance_t *core;
  libvlc_media_player_t *mp;
  libvlc_exception_t ex;
} vlc_t;

/* private functions */
static init_status_t
vlc_init (player_t *player)
{
  vlc_t *vlc = NULL;
  const char *vlc_argv[32] = { "vlc" };
  int vlc_argc = 1;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  /* only X11 video out is supported right now */
  if (player->vo != PLAYER_VO_NULL &&
      player->vo != PLAYER_VO_X11 &&
      player->vo != PLAYER_VO_XV &&
      player->vo != PLAYER_VO_GL)
    return PLAYER_INIT_ERROR;

  //vlc_argv[vlc_argc++] = "-vv";
  vlc_argv[vlc_argc++] = "--no-stats";
  vlc_argv[vlc_argc++] = "--intf";
  vlc_argv[vlc_argc++] = "dummy";
  
  /* select the video output */
  switch (player->vo)
  {
  case PLAYER_VO_NULL:
    vlc_argv[vlc_argc++] = "--vout";
    vlc_argv[vlc_argc++] = "dummy";
    break;
    
  case PLAYER_VO_X11:
    vlc_argv[vlc_argc++] = "--vout";
    vlc_argv[vlc_argc++] = "x11,dummy";
    break;
    
  case PLAYER_VO_XV:
    vlc_argv[vlc_argc++] = "--vout";
    vlc_argv[vlc_argc++] = "xvideo,dummy";
    break;
    
  case PLAYER_VO_GL:
    vlc_argv[vlc_argc++] = "--vout";
    vlc_argv[vlc_argc++] = "glx,dummy";
    break;
    
  default:
    break;
  }

  /* select the audio output */
  switch (player->ao)
  {
  case PLAYER_AO_NULL:
    vlc_argv[vlc_argc++] = "--no-audio";
    break;
    
  case PLAYER_AO_ALSA:
    vlc_argv[vlc_argc++] = "--aout";
    vlc_argv[vlc_argc++] = "alsa,dummy";
    break;
    
  case PLAYER_AO_OSS:
    vlc_argv[vlc_argc++] = "--aout";
    vlc_argv[vlc_argc++] = "oss,dummy";
    break;

  default:
    break;
  }
  
  vlc = (vlc_t *) player->priv;
  libvlc_exception_init (&vlc->ex);
  vlc->core = libvlc_new (vlc_argc, vlc_argv, &vlc->ex);
  if (!vlc->core)
    return PLAYER_INIT_ERROR;

  if (libvlc_exception_raised (&vlc->ex))
  {
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, libvlc_exception_get_message (&vlc->ex));
    libvlc_exception_clear (&vlc->ex);
  }

  return PLAYER_INIT_OK;
}

static void
vlc_uninit (player_t *player)
{
  vlc_t *vlc = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "uninit");

  if (!player)
    return;

  vlc = (vlc_t *) player->priv;

  if (!vlc)
    return;

  libvlc_exception_clear (&vlc->ex);
  if (vlc->core)
    libvlc_release (vlc->core);
  free (vlc);
}

static int
vlc_mrl_supported_res (player_t *player, mrl_resource_t res)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "mrl_supported_res");

  if (!player)
    return 0;

  switch (res)
  {
  case MRL_RESOURCE_FILE:
    return 1;

  default:
    return 0;
  }
}

static char *
vlc_resource_get_uri (mrl_t *mrl)
{
  if (!mrl)
    return NULL;

  switch (mrl->resource)
  {
  case MRL_RESOURCE_FILE:
  {
    mrl_resource_local_args_t *args = mrl->priv;

    if (!args || !args->location)
      return NULL;

    return strdup (args->location);
  }

  default:
    break;
  }

  return NULL;
}

static playback_status_t
vlc_playback_start (player_t *player)
{
  vlc_t *vlc;
  mrl_t *mrl;
  char *uri = NULL;
  libvlc_media_t *media = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_start");

  if (!player)
    return PLAYER_PB_FATAL;

  vlc = (vlc_t *) player->priv;

  if (!vlc->core)
    return PLAYER_PB_ERROR;

  mrl = playlist_get_mrl (player->playlist);
  if (!mrl)
    return PLAYER_PB_ERROR;

  uri = vlc_resource_get_uri (mrl);
  if (!uri)
    return PLAYER_PB_ERROR;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "uri: %s", uri);
  media = libvlc_media_new (vlc->core, uri, &vlc->ex);
  free (uri);

  if (!media)
    return PLAYER_PB_ERROR;
  
  vlc->mp = libvlc_media_player_new_from_media (media, &vlc->ex);
  libvlc_media_release (media);
  libvlc_media_player_play (vlc->mp, &vlc->ex);
  
  return PLAYER_PB_OK;
}

static void
vlc_playback_stop (player_t *player)
{
  vlc_t *vlc;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_stop");

  if (!player)
    return;

  vlc = (vlc_t *) player->priv;

  libvlc_media_player_stop (vlc->mp, &vlc->ex);
  libvlc_media_player_release (vlc->mp);
  vlc->mp = NULL;
}

/* public API */
player_funcs_t *
register_functions_vlc (void)
{
  player_funcs_t *funcs = NULL;

  funcs = calloc (1, sizeof (player_funcs_t));
  if (!funcs)
    return NULL;

  funcs->init               = vlc_init;
  funcs->uninit             = vlc_uninit;
  funcs->set_verbosity      = NULL;

  funcs->mrl_supported_res  = vlc_mrl_supported_res;
  funcs->mrl_retrieve_props = NULL;
  funcs->mrl_retrieve_meta  = NULL;

  funcs->get_time_pos       = NULL;
  funcs->set_framedrop      = NULL;

  funcs->pb_start           = vlc_playback_start;
  funcs->pb_stop            = vlc_playback_stop;
  funcs->pb_pause           = NULL;
  funcs->pb_seek            = NULL;
  funcs->pb_seek_chapter    = NULL;
  funcs->pb_set_speed       = NULL;

  funcs->audio_get_volume   = NULL;
  funcs->audio_set_volume   = NULL;
  funcs->audio_get_mute     = NULL;
  funcs->audio_set_mute     = NULL;
  funcs->audio_set_delay    = NULL;
  funcs->audio_select       = NULL;
  funcs->audio_prev         = NULL;
  funcs->audio_next         = NULL;

  funcs->video_set_fs       = NULL;
  funcs->video_set_aspect   = NULL;
  funcs->video_set_panscan  = NULL;
  funcs->video_set_ar       = NULL;

  funcs->sub_set_delay      = NULL;
  funcs->sub_set_alignment  = NULL;
  funcs->sub_set_pos        = NULL;
  funcs->sub_set_visibility = NULL;
  funcs->sub_scale          = NULL;
  funcs->sub_select         = NULL;
  funcs->sub_prev           = NULL;
  funcs->sub_next           = NULL;

  funcs->dvd_nav            = NULL;
  funcs->dvd_angle_set      = NULL;
  funcs->dvd_angle_prev     = NULL;
  funcs->dvd_angle_next     = NULL;
  funcs->dvd_title_set      = NULL;
  funcs->dvd_title_prev     = NULL;
  funcs->dvd_title_next     = NULL;

  funcs->tv_channel_set     = NULL;
  funcs->tv_channel_prev    = NULL;
  funcs->tv_channel_next    = NULL;

  funcs->radio_channel_set  = NULL;
  funcs->radio_channel_prev = NULL;
  funcs->radio_channel_next = NULL;

  return funcs;
}

void *
register_private_vlc (void)
{
  vlc_t *vlc = NULL;

  vlc = calloc (1, sizeof (vlc_t));
  if (!vlc)
    return NULL;

  vlc->core = NULL;
  vlc->mp   = NULL;

  return vlc;
}
