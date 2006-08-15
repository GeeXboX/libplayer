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

#include "player.h"
#include "logs.h"

#define MODULE_NAME "player"

static struct mrl_t *
mrl_new (struct player_t *player, char *name,
         player_mrl_type_t type, char *cover)
{
  struct mrl_t *mrl = NULL;

  /* check for minimal requirements */
  if (!player || !name)
    return NULL;
  
  mrl = (struct mrl_t *) malloc (sizeof (struct mrl_t));
  mrl->name = strdup (name);
  mrl->cover = cover ? strdup (cover) : NULL;
  mrl->type = type;
  mrl->prop = NULL;
  mrl->meta = NULL;
  mrl->prev = NULL;
  mrl->next = NULL;

  /* get MRL properties if available */

  /* get MRL metadata if available */
  
  return mrl;
}

static void
mrl_free (struct mrl_t *mrl, int recursive)
{
  if (!mrl)
    return;
  
  if (mrl->name)
    free (mrl->name);
  if (mrl->cover)
    free (mrl->cover);

  /*
  if (mrl->prop)
    mrl_properties_free (mrl->prop);
  if (mrl->meta)
    mrl_metadata_free (mrl->prop);
  */
  
  if (recursive && mrl->next)
    mrl_free (mrl->next, 1);
    
  free (mrl);
}

void
mrl_list_free (struct mrl_t *mrl)
{
  /* go to the very begining of the playlist in case of recursive free() */
  while (mrl->prev)
    mrl = mrl->prev;

  mrl_free (mrl, 1);
}

void
player_mrl_append (struct player_t *player,
                   char *location, player_mrl_type_t type,
                   char *cover, player_add_mrl_t when)
{
  struct mrl_t *mrl = NULL;

  plog (MODULE_NAME, "player_mrl_append");
    
  if (!player || !location)
    return;

  mrl = mrl_new (player, location, type, cover);
  if (!mrl)
    return;
  
  /* create/expand the playlist */
  if (!player->mrl) /* empty list */
    player->mrl = mrl;
  else /* create double-linked playlist, appending new MRL to the bottom */
  {
    struct mrl_t *list;

    list = player->mrl;
    while (list->next)
      list = list->next;
    list->next = mrl;
    mrl->prev = list;
  }

  /* play it now ? */
  if (when == PLAYER_ADD_MRL_NOW)
  {
    player_playback_stop (player);
    player->mrl = mrl;
    player_playback_start (player);
  }
}
   
void
player_mrl_previous (struct player_t *player)
{
  struct mrl_t *mrl;

  plog (MODULE_NAME, "player_mrl_previous");
  
  if (!player)
    return;

  mrl = player->mrl;
  if (!mrl || !mrl->prev)
    return;

  player_playback_stop (player);
  player->mrl = mrl->prev;
  player_playback_start (player);
}

void
player_mrl_next (struct player_t *player)
{
  struct mrl_t *mrl;

  plog (MODULE_NAME, "player_mrl_next");
  
  if (!player)
    return;

  mrl = player->mrl;
  if (!mrl || !mrl->next)
    return;

  player_playback_stop (player);
  player->mrl = mrl->next;
  player_playback_start (player);
}
