/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2008 Mathieu Schroeter <mathieu.schroeter@gamesover.ch>
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

#include "player.h"
#include "player_internals.h"
#include "playlist.h"

struct playlist_s {
  mrl_t *mrl_list;
  int shuffle;
  int loop;
  player_loop_t loop_mode;
};


playlist_t *
playlist_new (int shuffle, int loop, player_loop_t loop_mode)
{
  playlist_t *playlist;

  playlist = calloc (1, sizeof (playlist_t));
  if (!playlist)
    return NULL;

  playlist->shuffle = shuffle;
  playlist->loop = loop;
  playlist->loop_mode = loop_mode;

  return playlist;
}

void
playlist_free (playlist_t *playlist)
{
  if (!playlist)
    return;

  if (playlist->mrl_list)
    mrl_list_free (playlist->mrl_list);

  free (playlist);
}

void
playlist_set_loop (playlist_t *playlist, int loop, player_loop_t mode)
{
  if (!playlist)
    return;

  playlist->loop = loop;
  playlist->loop_mode = mode;
}

void
playlist_set_shuffle (playlist_t *playlist, int shuffle)
{
  if (!playlist)
    return;

  playlist->shuffle = shuffle;
}

int
playlist_count_mrl (playlist_t *playlist)
{
  int cnt = 1;
  mrl_t *mrl;

  if (!playlist || !playlist->mrl_list)
    return 0;

  mrl = playlist->mrl_list;
  while (mrl->next)
  {
    mrl = mrl->next;
    cnt++;
  }

  return cnt;
}

mrl_t *
playlist_get_mrl (playlist_t *playlist)
{
  mrl_t *mrl;

  if (!playlist)
    return NULL;

  mrl = playlist->mrl_list;

  return mrl;
}

void
playlist_set_mrl (playlist_t *playlist, mrl_t *mrl)
{
  mrl_t *mrl_list;

  if (!playlist || !mrl)
    return;

  mrl_list = playlist->mrl_list;
  if (mrl_list)
  {
    mrl->prev = mrl_list->prev;
    mrl->next = mrl_list->next;
    mrl_free (mrl_list, 0);
    playlist->mrl_list = mrl;
  }
  else
    playlist->mrl_list = mrl;
}

void
playlist_append_mrl (playlist_t *playlist, mrl_t *mrl)
{
  mrl_t *mrl_list;

  if (!playlist || !mrl)
    return;

  mrl_list = playlist->mrl_list;
  if (mrl_list)
  {
    while (mrl_list->next)
      mrl_list = mrl_list->next;

    mrl_list->next = mrl;
    mrl->prev = mrl_list;
  }
  else
    playlist->mrl_list = mrl;
}

int
playlist_next_mrl_available (playlist_t *playlist)
{
  int res = 0;

  if (!playlist)
    return 0;

  if (playlist->mrl_list && playlist->mrl_list->next)
    res = 1;

  return res;
}

void
playlist_next_mrl (playlist_t *playlist)
{
  if (!playlist)
    return;

  if (playlist->mrl_list && playlist->mrl_list->next)
    playlist->mrl_list = playlist->mrl_list->next;
}

int
playlist_previous_mrl_available (playlist_t *playlist)
{
  int res = 0;

  if (!playlist)
    return 0;

  if (playlist->mrl_list && playlist->mrl_list->prev)
    res = 1;

  return res;
}

void
playlist_previous_mrl (playlist_t *playlist)
{
  if (!playlist)
    return;

  if (playlist->mrl_list && playlist->mrl_list->prev)
    playlist->mrl_list = playlist->mrl_list->prev;
}

void
playlist_last_mrl (playlist_t *playlist)
{
  mrl_t *mrl;

  if (!playlist)
    return;

  mrl = playlist->mrl_list;
  while (mrl->next)
    mrl = mrl->next;
  playlist->mrl_list = mrl;
}

void
playlist_remove_mrl (playlist_t *playlist)
{
  mrl_t *mrl, *mrl_p, *mrl_n;

  if (!playlist || !playlist->mrl_list)
    return;

  mrl = playlist->mrl_list;
  mrl_p = mrl->prev;
  mrl_n = mrl->next;

  mrl_free (mrl, 0);

  /* link previous with the next and use the next as the current MRL */
  if (mrl_p && mrl_n) {
    mrl_p->next = mrl_n;
    mrl_n->prev = mrl_p;
    playlist->mrl_list = mrl_n;
  }
  /* use the previous as the current MRL */
  else if (mrl_p) {
    mrl_p->next = NULL;
    playlist->mrl_list = mrl_p;
  }
  /* use the next as the current MRL */
  else if (mrl_n) {
    mrl_n->prev = NULL;
    playlist->mrl_list = mrl_n;
  }
  else
    playlist->mrl_list = NULL;
}

void
playlist_empty (playlist_t *playlist)
{
  if (!playlist)
    return;

  if (playlist->mrl_list)
  {
    mrl_list_free (playlist->mrl_list);
    playlist->mrl_list = NULL;
  }
}
