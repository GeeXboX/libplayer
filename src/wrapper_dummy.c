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

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "wrapper_dummy.h"

#define MODULE_NAME "dummy"

/* player specific structure */
typedef struct dummy_s {
  int dummy_var;
} dummy_t;

/* private functions */
static init_status_t
dummy_init (player_t *player)
{
  dummy_t *dummy = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  dummy = (dummy_t *) player->priv;
  dummy->dummy_var = 1;

  return PLAYER_INIT_OK;
}

static void
dummy_uninit (player_t *player)
{
  dummy_t *dummy = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "uninit");

  if (!player)
    return;

  dummy = (dummy_t *) player->priv;

  if (!dummy)
    return;

  free (dummy);
}

static void
dummy_mrl_retrieve_properties (player_t *player, mrl_t *mrl)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "mrl_retrieve_properties");
}

static void
dummy_mrl_retrieve_metadata (player_t *player, mrl_t *mrl)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "mrl_retrieve_metadata");
}

static playback_status_t
dummy_playback_start (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_start");
  return PLAYER_PB_OK;
}

static void
dummy_playback_stop (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_stop");
}

static playback_status_t
dummy_playback_pause (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_pause");
  return PLAYER_PB_OK;
}

static void
dummy_playback_seek (player_t *player, int value, player_pb_seek_t seek)
{
  plog (player, PLAYER_MSG_INFO,
        MODULE_NAME, "playback_seek: %d %d", value, seek);
}

static void
dummy_dvd_nav (player_t *player, player_dvdnav_t value)
{
  char log[8] = "unknown";

  switch (value)
  {
  case PLAYER_DVDNAV_UP:
    strcpy (log, "up");
    break;

  case PLAYER_DVDNAV_DOWN:
    strcpy (log, "down");
    break;

  case PLAYER_DVDNAV_LEFT:
    strcpy (log, "left");
    break;

  case PLAYER_DVDNAV_RIGHT:
    strcpy (log, "right");
    break;

  case PLAYER_DVDNAV_MENU:
    strcpy (log, "menu");
    break;

  case PLAYER_DVDNAV_SELECT:
    strcpy (log, "select");
  }

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "dvd_nav: %s", log);
}

static int
dummy_get_volume (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "get_volume");
  return 0;
}

static player_mute_t
dummy_get_mute (player_t *player)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "get_mute");
  return PLAYER_MUTE_OFF;
}

static void
dummy_set_volume (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "set_volume: %d", value);
}

static void
dummy_set_mute (player_t *player, player_mute_t value)
{
  if (value == PLAYER_MUTE_UNKNOWN)
    return;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME,
        "set_mute: %s", value == PLAYER_MUTE_ON ? "on" : "off");
}

static void
dummy_sub_set_delay (player_t *player, int value)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "sub_set_delay: %i", value);
}

/* public API */
player_funcs_t *
register_functions_dummy (void)
{
  player_funcs_t *funcs = NULL;

  funcs = calloc (1, sizeof (player_funcs_t));

  funcs->init               = dummy_init;
  funcs->uninit             = dummy_uninit;
  funcs->set_verbosity      = NULL;

  funcs->mrl_supported_res  = NULL;
  funcs->mrl_retrieve_props = dummy_mrl_retrieve_properties;
  funcs->mrl_retrieve_meta  = dummy_mrl_retrieve_metadata;

  funcs->get_time_pos       = NULL;
  funcs->set_framedrop      = NULL;

  funcs->pb_start           = dummy_playback_start;
  funcs->pb_stop            = dummy_playback_stop;
  funcs->pb_pause           = dummy_playback_pause;
  funcs->pb_seek            = dummy_playback_seek;
  funcs->pb_seek_chapter    = NULL;
  funcs->pb_set_speed       = NULL;

  funcs->get_volume         = dummy_get_volume;
  funcs->set_volume         = dummy_set_volume;
  funcs->get_mute           = dummy_get_mute;
  funcs->set_mute           = dummy_set_mute;
  funcs->audio_set_delay    = NULL;
  funcs->audio_select       = NULL;
  funcs->audio_prev         = NULL;
  funcs->audio_next         = NULL;

  funcs->video_set_fs       = NULL;
  funcs->video_set_aspect   = NULL;
  funcs->video_set_panscan  = NULL;
  funcs->video_set_ar       = NULL;

  funcs->sub_set_delay      = dummy_sub_set_delay;
  funcs->sub_set_alignment  = NULL;
  funcs->sub_set_pos        = NULL;
  funcs->sub_set_visibility = NULL;
  funcs->sub_scale          = NULL;
  funcs->sub_select         = NULL;
  funcs->sub_prev           = NULL;
  funcs->sub_next           = NULL;

  funcs->dvd_nav            = dummy_dvd_nav;
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
register_private_dummy (void)
{
  dummy_t *dummy = NULL;

  dummy = malloc (sizeof (dummy_t));
  dummy->dummy_var = 0;

  return dummy;
}
