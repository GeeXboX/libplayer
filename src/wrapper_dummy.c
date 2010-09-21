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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "init");

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

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "uninit");

  if (!player)
    return;

  dummy = (dummy_t *) player->priv;

  PFREE (dummy);
}

static void
dummy_mrl_retrieve_properties (player_t *player, pl_unused mrl_t *mrl)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "mrl_retrieve_properties");
}

static void
dummy_mrl_retrieve_metadata (player_t *player, pl_unused mrl_t *mrl)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "mrl_retrieve_metadata");
}

static playback_status_t
dummy_playback_start (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_start");
  return PLAYER_PB_OK;
}

static void
dummy_playback_stop (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_stop");
}

static playback_status_t
dummy_playback_pause (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_pause");
  return PLAYER_PB_OK;
}

static void
dummy_playback_seek (player_t *player, int value, player_pb_seek_t seek)
{
  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "playback_seek: %d %d", value, seek);
}

static void
dummy_dvd_nav (player_t *player, player_dvdnav_t value)
{
  char log[16] = "unknown";

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
    break;

  case PLAYER_DVDNAV_PREVMENU:
    strcpy (log, "prevmenu");
    break;

  case PLAYER_DVDNAV_MOUSECLICK:
    strcpy (log, "mouseclick");
    break;
  }

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "dvd_nav: %s", log);
}

static int
dummy_audio_get_volume (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "audio_get_volume");
  return 0;
}

static player_mute_t
dummy_audio_get_mute (player_t *player)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "audio_get_mute");
  return PLAYER_MUTE_OFF;
}

static void
dummy_audio_set_volume (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "audio_set_volume: %d", value);
}

static void
dummy_audio_set_mute (player_t *player, player_mute_t value)
{
  if (value == PLAYER_MUTE_UNKNOWN)
    return;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME,
          "audio_set_mute: %s", value == PLAYER_MUTE_ON ? "on" : "off");
}

static void
dummy_sub_set_delay (player_t *player, int value)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "sub_set_delay: %i", value);
}

/*****************************************************************************/
/*                            Public Wrapper API                             */
/*****************************************************************************/

int
pl_supported_resources_dummy (pl_unused mrl_resource_t mrl)
{
  return 1;
}

player_funcs_t *
pl_register_functions_dummy (void)
{
  player_funcs_t *funcs = NULL;

  funcs = PCALLOC (player_funcs_t, 1);
  if (!funcs)
    return NULL;

  funcs->init               = dummy_init;
  funcs->uninit             = dummy_uninit;
  funcs->set_verbosity      = NULL;

  funcs->mrl_retrieve_props = dummy_mrl_retrieve_properties;
  funcs->mrl_retrieve_meta  = dummy_mrl_retrieve_metadata;
  funcs->mrl_video_snapshot = NULL;

  funcs->get_time_pos       = NULL;
  funcs->get_percent_pos    = NULL;
  funcs->set_framedrop      = NULL;
  funcs->set_mouse_pos      = NULL;
  funcs->osd_show_text      = NULL;
  funcs->osd_state          = NULL;

  funcs->pb_start           = dummy_playback_start;
  funcs->pb_stop            = dummy_playback_stop;
  funcs->pb_pause           = dummy_playback_pause;
  funcs->pb_seek            = dummy_playback_seek;
  funcs->pb_seek_chapter    = NULL;
  funcs->pb_set_speed       = NULL;

  funcs->audio_get_volume   = dummy_audio_get_volume;
  funcs->audio_set_volume   = dummy_audio_set_volume;
  funcs->audio_get_mute     = dummy_audio_get_mute;
  funcs->audio_set_mute     = dummy_audio_set_mute;
  funcs->audio_set_delay    = NULL;
  funcs->audio_select       = NULL;
  funcs->audio_prev         = NULL;
  funcs->audio_next         = NULL;

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

  funcs->vdr                = NULL;

  return funcs;
}

void *
pl_register_private_dummy (void)
{
  dummy_t *dummy = NULL;

  dummy = malloc (sizeof (dummy_t));
  if (!dummy)
    return NULL;

  dummy->dummy_var = 0;

  return dummy;
}
