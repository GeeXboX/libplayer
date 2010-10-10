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

#ifndef PLAYLIST_H
#define PLAYLIST_H

typedef struct playlist_s playlist_t;

playlist_t *pl_playlist_new (int shuffle, int loop, player_loop_t loop_mode);
void pl_playlist_free (playlist_t *playlist);
void pl_playlist_set_loop (playlist_t *playlist, int loop, player_loop_t mode);
void pl_playlist_set_shuffle (playlist_t *playlist, int shuffle);
int pl_playlist_next_play (playlist_t *playlist);
int pl_playlist_count_mrl (playlist_t *playlist);
mrl_t *pl_playlist_get_mrl (playlist_t *playlist);
void pl_playlist_set_mrl (playlist_t *playlist, mrl_t *mrl);
void pl_playlist_append_mrl (playlist_t *playlist, mrl_t *mrl);
int pl_playlist_next_mrl_available (playlist_t *playlist);
void pl_playlist_next_mrl (playlist_t *playlist);
int pl_playlist_previous_mrl_available (playlist_t *playlist);
void pl_playlist_previous_mrl (playlist_t *playlist);
void pl_playlist_first_mrl (playlist_t *playlist);
void pl_playlist_last_mrl (playlist_t *playlist);
void pl_playlist_remove_mrl (playlist_t *playlist);
void pl_playlist_empty (playlist_t *playlist);

#endif /* PLAYLIST_H */
