/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006 Benjamin Zores <ben@geexbox.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include <stdlib.h>
#include <inttypes.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "wrapper_dummy.h"

#define MODULE_NAME "dummy"

/* player specific structure */
struct dummy_t {
  int dummy_var;
};

/* private functions */
static init_status_t
dummy_init (struct player_t *player)
{
  struct dummy_t *dummy = NULL;
  
  plog (MODULE_NAME, "init");
  
  if (!player)
    return PLAYER_INIT_ERROR;

  dummy = (struct dummy_t *) player->priv;
  dummy->dummy_var = 1;

  return PLAYER_INIT_OK;
}

static void
dummy_uninit (void *priv)
{
  struct dummy_t *dummy = NULL;
  
  plog (MODULE_NAME, "uninit");
  
  if (!priv)
    return;

  dummy = (struct dummy_t *) priv;
  free (dummy);
}

static void
dummy_mrl_get_properties (struct player_t *player, struct mrl_t *mrl)
{
  plog (MODULE_NAME, "mrl_get_properties");
}

static void
dummy_mrl_get_metadata (struct player_t *player, struct mrl_t *mrl)
{
  plog (MODULE_NAME, "mrl_get_metadata");
}

static playback_status_t
dummy_playback_start (struct player_t *player)
{
  plog (MODULE_NAME, "playback_start");
  return PLAYER_PB_OK;
}

static void
dummy_playback_stop (struct player_t *player)
{
  plog (MODULE_NAME, "playback_stop");
}

static playback_status_t
dummy_playback_pause (struct player_t *player)
{
  plog (MODULE_NAME, "playback_pause");
  return PLAYER_PB_OK;
}

static void
dummy_playback_seek (struct player_t *player, int value)
{
  plog (MODULE_NAME, "playback_seek: %d", value);
}

static int
dummy_get_volume (struct player_t *player)
{
  plog (MODULE_NAME, "get_volume");
  return 0;
}

static void
dummy_set_volume (struct player_t *player, int value)
{
  plog (MODULE_NAME, "set_volume: %d", value);
}

/* public API */
struct player_funcs_t *
register_functions_dummy (void)
{
  struct player_funcs_t *funcs = NULL;

  funcs = (struct player_funcs_t *) malloc (sizeof (struct player_funcs_t));
  funcs->init = dummy_init;
  funcs->uninit = dummy_uninit;
  funcs->mrl_get_props = dummy_mrl_get_properties;
  funcs->mrl_get_meta = dummy_mrl_get_metadata;
  funcs->pb_start = dummy_playback_start;
  funcs->pb_stop = dummy_playback_stop;
  funcs->pb_pause = dummy_playback_pause;
  funcs->pb_seek = dummy_playback_seek;
  funcs->get_volume = dummy_get_volume;
  funcs->set_volume = dummy_set_volume;
  
  return funcs;
}

void *
register_private_dummy (void)
{
  struct dummy_t *dummy = NULL;

  dummy = (struct dummy_t *) malloc (sizeof (struct dummy_t));
  dummy->dummy_var = 0;

  return dummy;
}
