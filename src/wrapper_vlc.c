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

  plog (MODULE_NAME, "init");

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
    plog (MODULE_NAME, libvlc_exception_get_message (&vlc->ex));
    libvlc_exception_clear (&vlc->ex);
  }

  return PLAYER_INIT_OK;
}

static void
vlc_uninit (player_t *player)
{
  vlc_t *vlc = NULL;

  plog (MODULE_NAME, "uninit");

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

static void
vlc_mrl_get_properties (player_t *player)
{
  plog (MODULE_NAME, "mrl_get_properties");
}

static void
vlc_mrl_get_metadata (player_t *player)
{
  plog (MODULE_NAME, "mrl_get_metadata");
}

static playback_status_t
vlc_playback_start (player_t *player)
{
  vlc_t *vlc;
  int id;

  plog (MODULE_NAME, "playback_start");

  if (!player)
    return PLAYER_PB_FATAL;

  vlc = (vlc_t *) player->priv;

  if (!vlc->core)
    return PLAYER_PB_ERROR;

  id = libvlc_playlist_add (vlc->core, player->mrl->name, NULL, NULL);
  libvlc_playlist_play (vlc->core, id, 0, NULL, NULL);

  return PLAYER_PB_OK;
}

static void
vlc_playback_stop (player_t *player)
{
  plog (MODULE_NAME, "playback_stop");
}

static playback_status_t
vlc_playback_pause (player_t *player)
{
  plog (MODULE_NAME, "playback_pause");
  return PLAYER_PB_OK;
}

static void
vlc_playback_seek (player_t *player, int value)
{
  plog (MODULE_NAME, "playback_seek: %d", value);
}

static void
vlc_playback_dvdnav (player_t *player, player_dvdnav_t value)
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

  plog (MODULE_NAME, "playback_dvdnav: %s", log);
}

static int
vlc_get_volume (player_t *player)
{
  plog (MODULE_NAME, "get_volume");
  return 0;
}

static player_mute_t
vlc_get_mute (player_t *player)
{
  plog (MODULE_NAME, "get_mute");
  return PLAYER_MUTE_OFF;
}

static void
vlc_set_volume (player_t *player, int value)
{
  plog (MODULE_NAME, "set_volume: %d", value);
}

static void
vlc_set_mute (player_t *player, player_mute_t value)
{
  if (value == PLAYER_MUTE_UNKNOWN)
    return;

  plog (MODULE_NAME, "set_mute: %s", value == PLAYER_MUTE_ON ? "on" : "off");
}

/* public API */
player_funcs_t *
register_functions_vlc (void)
{
  player_funcs_t *funcs = NULL;

  funcs = malloc (sizeof (player_funcs_t));
  funcs->init = vlc_init;
  funcs->uninit = vlc_uninit;
  funcs->mrl_get_props = vlc_mrl_get_properties;
  funcs->mrl_get_meta = vlc_mrl_get_metadata;
  funcs->pb_start = vlc_playback_start;
  funcs->pb_stop = vlc_playback_stop;
  funcs->pb_pause = vlc_playback_pause;
  funcs->pb_seek = vlc_playback_seek;
  funcs->pb_dvdnav = vlc_playback_dvdnav;
  funcs->get_volume = vlc_get_volume;
  funcs->get_mute = vlc_get_mute;
  funcs->set_volume = vlc_set_volume;
  funcs->set_mute = vlc_set_mute;

  return funcs;
}

void *
register_private_vlc (void)
{
  vlc_t *vlc = NULL;

  vlc = malloc (sizeof (vlc_t));
  vlc->core = NULL;

  return vlc;
}
