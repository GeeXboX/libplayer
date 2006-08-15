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
#include <string.h>
#include <stdio.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"

/* players wrappers */
#include "wrapper_dummy.h"
#ifdef HAVE_XINE
#include "wrapper_xine.h"
#endif /* HAVE_XINE */

#define MODULE_NAME "player"

struct player_t *
player_init (player_type_t type, char *ao, char *vo,
             int event_cb (player_event_t e, void *data))
{
  struct player_t *player = NULL;
  int res = PLAYER_INIT_ERROR;
  
  player = (struct player_t *) malloc (sizeof (struct player_t));
  player->type = type;
  player->mrl = NULL;  
  player->state = PLAYER_STATE_IDLE;
  player->loop = 0;
  player->shuffle = 0;
  player->ao = ao ? strdup (ao) : NULL;
  player->vo = vo ? strdup (vo) : NULL;
  player->x = 0;
  player->y = 0;
  player->w = 0;
  player->h = 0;
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
player_uninit (struct player_t *player)
{
  if (!player)
    return;

  if (player->mrl)
    mrl_list_free (player->mrl);

  /* free player specific private properties */
  if (player->funcs->uninit)
    player->funcs->uninit (player->priv);
  
  free (player->funcs);
  free (player);
}

void
player_playback_start (struct player_t *player)
{
  int res = PLAYER_PB_ERROR;
  
  if (!player)
    return;
  
  if (player->state != PLAYER_STATE_IDLE) /* already running : stop it */
    player_playback_stop (player);

  if (!player->mrl) /* nothing to playback */
    return;

  /* player specific playback_start() */
  if (player->funcs->pb_start)
    res = player->funcs->pb_start (player);

  if (res != PLAYER_PB_OK)
    return;
  
  player->state = PLAYER_STATE_RUNNING;
  plog (MODULE_NAME, "Now playing: %s", player->mrl->name);

  /* notify front-end */
  if (player->event_cb)
    player->event_cb (PLAYER_EVENT_PLAYBACK_START, NULL);
}

void
player_playback_stop (struct player_t *player)
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
player_playback_pause (struct player_t *player)
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
player_playback_seek (struct player_t *player, int value)
{
  if (!player)
    return;

  /* player specific playback_seek() */
  if (player->funcs->pb_seek)
    player->funcs->pb_seek (player, value);
}

/* get player playback properties */
int
player_get_volume (struct player_t *player)
{
  int res = -1;
  
  if (!player)
    return -1;

  /* player specific get_volume() */
  if (player->funcs->get_volume)
    res = player->funcs->get_volume (player);

  return res;
}

/* tune player playback properties */
void
player_set_loop (struct player_t *player, int value)
{
  if (!player)
    return;

  player->loop = value;
}

void
player_set_shuffle (struct player_t *player, int value)
{
  if (!player)
    return;

  player->shuffle = value;
}

void
player_set_volume (struct player_t *player, int value)
{
  if (!player)
    return;

  /* player specific set_volume() */
  if (player->funcs->set_volume)
    player->funcs->set_volume (player, value);
}
