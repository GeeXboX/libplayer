/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2008 Mathieu Schroeter <mathieu@schroetersa.ch>
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
#include <time.h> /* time */

#include "player.h"
#include "player_internals.h"
#include "playlist.h"

struct playlist_s {
  mrl_t *mrl_list;
  int    reset;

  int          shuffle;
  int         *shuffle_list;
  int          shuffle_it;
  int          shuffle_cnt;
  unsigned int shuffle_seed;

  int           loop;
  int           loop_cnt;
  player_loop_t loop_mode;
};


playlist_t *
pl_playlist_new (int shuffle, int loop, player_loop_t loop_mode)
{
  playlist_t *playlist;

  playlist = PCALLOC (playlist_t, 1);
  if (!playlist)
    return NULL;

  pl_playlist_set_shuffle (playlist, shuffle);
  pl_playlist_set_loop (playlist, loop, loop_mode);

  return playlist;
}

void
pl_playlist_free (playlist_t *playlist)
{
  if (!playlist)
    return;

  if (playlist->mrl_list)
    mrl_list_free (playlist->mrl_list);

  PFREE (playlist->shuffle_list);
  PFREE (playlist);
}

void
pl_playlist_set_loop (playlist_t *playlist, int loop, player_loop_t mode)
{
  if (!playlist)
    return;

  playlist->loop      = loop > 0 ? --loop : loop;
  playlist->loop_cnt  = loop;
  playlist->loop_mode = mode;
}

static void
playlist_goto_mrl (playlist_t *playlist, int value)
{
  if (!playlist)
    return;

  pl_playlist_first_mrl (playlist);
  while (pl_playlist_next_mrl_available (playlist) && value--)
    pl_playlist_next_mrl (playlist);
}

static void
playlist_shuffle_init (playlist_t *playlist)
{
  int i;

  if (!playlist)
    return;

  if (!playlist->shuffle_seed)
    playlist->shuffle_seed = time (NULL);

  playlist->shuffle_cnt = pl_playlist_count_mrl (playlist);

  PFREE (playlist->shuffle_list);

  playlist->shuffle_list = malloc (playlist->shuffle_cnt * sizeof (int));
  if (!playlist->shuffle_list)
    return;

  /* init list */
  for (i = 0; i < playlist->shuffle_cnt; i++)
    *(playlist->shuffle_list + i) = i;

  /* pseudo-random shuffle */
  for (i = 0; i < (playlist->shuffle_cnt - 1); i++)
  {
    int r, tmp;
    r = i + (int) (rand_r (&playlist->shuffle_seed)
                   / (double) RAND_MAX * (playlist->shuffle_cnt - i));
    tmp = *(playlist->shuffle_list + i);
    *(playlist->shuffle_list + i) = *(playlist->shuffle_list + r);
    *(playlist->shuffle_list + r) = tmp;
  }

  playlist->shuffle_it = 0;
  playlist_goto_mrl (playlist, *playlist->shuffle_list); /* first mrl to play */
}

void
pl_playlist_set_shuffle (playlist_t *playlist, int shuffle)
{
  if (!playlist)
    return;

  playlist->shuffle = shuffle;

  if (shuffle)
    playlist_shuffle_init (playlist);
}

static int
playlist_shuffle_next_available (playlist_t *playlist)
{
  if (!playlist)
    return 0;

  return playlist->shuffle_it < playlist->shuffle_cnt - 1;
}

static void
playlist_shuffle_next (playlist_t *playlist)
{
  int *value;

  if (!playlist)
    return;

  if (!playlist_shuffle_next_available (playlist))
    return;

  playlist->shuffle_it++;
  value = playlist->shuffle_list + playlist->shuffle_it;
  playlist_goto_mrl (playlist, *value);
}

static void
playlist_reset_counter (playlist_t *playlist, player_loop_t mode)
{
  if (mode != playlist->loop_mode)
    return;

  playlist->loop_cnt = playlist->loop; /* reset the loop counter */
}

int
pl_playlist_next_play (playlist_t *playlist)
{
  if (!playlist)
    return 0;

  if (playlist->reset) /* manual start? ok, then reset */
  {
    playlist_reset_counter (playlist, playlist->loop_mode);
    playlist->reset = 0;
  }

  switch (playlist->loop_mode)
  {
  case PLAYER_LOOP_ELEMENT:
    if (!playlist->loop_cnt)
    {
      playlist->reset = 1;
      return 0; /* end loop */
    }

    if (playlist->loop_cnt < 0) /* infinite */
      return 1; /* same mrl */

    playlist->loop_cnt--;
    return 1; /* same mrl */

  case PLAYER_LOOP_PLAYLIST:
    if (playlist->shuffle)
    {
      if (playlist_shuffle_next_available (playlist))
        break; /* next mrl */
    }
    else if (pl_playlist_next_mrl_available (playlist))
      break; /* next mrl */

    if (!playlist->loop_cnt)
    {
      playlist->reset = 1;
      if (playlist->shuffle)
        playlist_shuffle_init (playlist);
      return 0; /* end loop */
    }

    if (playlist->loop_cnt > 0) /* else: infinite */
      playlist->loop_cnt--;

    if (playlist->shuffle)
      playlist_shuffle_init (playlist);
    else
      pl_playlist_first_mrl (playlist);
    return 1; /* first mrl */

  case PLAYER_LOOP_DISABLE:
  default:
    break; /* next mrl */
  }

  if (playlist->shuffle)
  {
    if (!playlist_shuffle_next_available (playlist))
    {
      playlist_shuffle_init (playlist);
      return 0;
    }
  }
  else if (!pl_playlist_next_mrl_available (playlist))
    return 0;

  if (playlist->shuffle)
    playlist_shuffle_next (playlist);
  else
    pl_playlist_next_mrl (playlist);
  return 1;
}

int
pl_playlist_count_mrl (playlist_t *playlist)
{
  int cnt = 1;
  mrl_t *mrl;

  if (!playlist || !playlist->mrl_list)
    return 0;

  mrl = playlist->mrl_list;
  while (mrl->prev)
    mrl = mrl->prev;

  while (mrl->next)
  {
    mrl = mrl->next;
    cnt++;
  }

  return cnt;
}

mrl_t *
pl_playlist_get_mrl (playlist_t *playlist)
{
  mrl_t *mrl;

  if (!playlist)
    return NULL;

  mrl = playlist->mrl_list;

  return mrl;
}

void
pl_playlist_set_mrl (playlist_t *playlist, mrl_t *mrl)
{
  mrl_t *mrl_list;

  if (!playlist || !mrl)
    return;

  mrl_list = playlist->mrl_list;
  if (mrl_list)
  {
    mrl->prev = mrl_list->prev;
    mrl->next = mrl_list->next;
    mrl_sv_free (mrl_list, 0);
    playlist->mrl_list = mrl;
  }
  else
    playlist->mrl_list = mrl;

  playlist_reset_counter (playlist, PLAYER_LOOP_ELEMENT);
}

void
pl_playlist_append_mrl (playlist_t *playlist, mrl_t *mrl)
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
  {
    playlist->mrl_list = mrl;
    playlist_reset_counter (playlist, PLAYER_LOOP_ELEMENT);
  }
}

int
pl_playlist_next_mrl_available (playlist_t *playlist)
{
  int res = 0;

  if (!playlist)
    return 0;

  if (playlist->mrl_list && playlist->mrl_list->next)
    res = 1;

  return res;
}

void
pl_playlist_next_mrl (playlist_t *playlist)
{
  if (!playlist)
    return;

  if (playlist->mrl_list && playlist->mrl_list->next)
  {
    playlist->mrl_list = playlist->mrl_list->next;
    playlist_reset_counter (playlist, PLAYER_LOOP_ELEMENT);
  }
}

int
pl_playlist_previous_mrl_available (playlist_t *playlist)
{
  int res = 0;

  if (!playlist)
    return 0;

  if (playlist->mrl_list && playlist->mrl_list->prev)
    res = 1;

  return res;
}

void
pl_playlist_previous_mrl (playlist_t *playlist)
{
  if (!playlist)
    return;

  if (playlist->mrl_list && playlist->mrl_list->prev)
  {
    playlist->mrl_list = playlist->mrl_list->prev;
    playlist_reset_counter (playlist, PLAYER_LOOP_ELEMENT);
  }
}

void
pl_playlist_first_mrl (playlist_t *playlist)
{
  mrl_t *mrl;

  if (!playlist || !playlist->mrl_list)
    return;

  mrl = playlist->mrl_list;
  while (mrl->prev)
    mrl = mrl->prev;
  playlist->mrl_list = mrl;

  playlist_reset_counter (playlist, PLAYER_LOOP_ELEMENT);
}

void
pl_playlist_last_mrl (playlist_t *playlist)
{
  mrl_t *mrl;

  if (!playlist || !playlist->mrl_list)
    return;

  mrl = playlist->mrl_list;
  while (mrl->next)
    mrl = mrl->next;
  playlist->mrl_list = mrl;

  playlist_reset_counter (playlist, PLAYER_LOOP_ELEMENT);
}

void
pl_playlist_remove_mrl (playlist_t *playlist)
{
  mrl_t *mrl, *mrl_p, *mrl_n;

  if (!playlist || !playlist->mrl_list)
    return;

  mrl = playlist->mrl_list;
  mrl_p = mrl->prev;
  mrl_n = mrl->next;

  mrl_sv_free (mrl, 0);

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

  playlist_reset_counter (playlist, PLAYER_LOOP_ELEMENT);
}

void
pl_playlist_empty (playlist_t *playlist)
{
  if (!playlist)
    return;

  if (playlist->mrl_list)
  {
    mrl_list_free (playlist->mrl_list);
    playlist->mrl_list = NULL;
  }

  playlist_reset_counter (playlist, PLAYER_LOOP_ELEMENT);
}
