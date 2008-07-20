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
#include <inttypes.h>
#include <string.h>
#include <vlc/libvlc.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "wrapper_vlc.h"

#define MODULE_NAME "vlc"

/* player specific structure */
typedef struct vlc_s {
  libvlc_instance_t *core;
  libvlc_exception_t ex;
} vlc_t;

/* private functions */
static init_status_t
vlc_init (player_t *player)
{
  vlc_t *vlc = NULL;
  char *vlc_argv[32] = { "vlc" };
  int vlc_argc = 1;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  //vlc_argv[vlc_argc++] = "-vv";
  vlc_argv[vlc_argc++] = "--no-stats";
  vlc_argv[vlc_argc++] = "--intf";
  vlc_argv[vlc_argc++] = "dummy";

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

  if (&vlc->ex)
    libvlc_exception_clear (&vlc->ex);
  if (vlc->core)
    libvlc_destroy (vlc->core);
  free (vlc);
}

static playback_status_t
vlc_playback_start (player_t *player)
{
  vlc_t *vlc;
#if 0
  int id;
#endif

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_start");

  if (!player)
    return PLAYER_PB_FATAL;

  vlc = (vlc_t *) player->priv;

  if (!vlc->core)
    return PLAYER_PB_ERROR;

#if 0
  id = libvlc_playlist_add (vlc->core, player->mrl->name, NULL, NULL);
  libvlc_playlist_play (vlc->core, id, 0, NULL, NULL);
#endif

  return PLAYER_PB_OK;
}

/* public API */
player_funcs_t *
register_functions_vlc (void)
{
  player_funcs_t *funcs = NULL;

  funcs = calloc (1, sizeof (player_funcs_t));

  funcs->init               = vlc_init;
  funcs->uninit             = vlc_uninit;
  funcs->set_verbosity      = NULL;

  funcs->mrl_supported_res  = NULL;
  funcs->mrl_retrieve_props = NULL;
  funcs->mrl_retrieve_meta  = NULL;

  funcs->get_time_pos       = NULL;
  funcs->set_framedrop      = NULL;

  funcs->pb_start           = vlc_playback_start;
  funcs->pb_stop            = NULL;
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
  vlc->core = NULL;

  return vlc;
}
