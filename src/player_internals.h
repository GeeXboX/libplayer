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

#ifndef _PLAYER_INTERNALS_H_
#define _PLAYER_INTERNALS_H_

typedef enum init_status_s init_status_t;
enum init_status_s {
  PLAYER_INIT_OK,
  PLAYER_INIT_ERROR
};

typedef enum playback_status_s playback_status_t;
enum playback_status_s {
  PLAYER_PB_OK,
  PLAYER_PB_FATAL,
  PLAYER_PB_ERROR
};

struct player_funcs_t {
  init_status_t (* init) (struct player_t *player);
  void (* uninit) (void *priv);
  void (* mrl_get_props) (struct player_t *player, struct mrl_t *mrl);
  void (* mrl_get_meta) (struct player_t *player, struct mrl_t *mrl);
  playback_status_t (* pb_start) (struct player_t *player);
  void (* pb_stop) (struct player_t *player);
  playback_status_t (* pb_pause) (struct player_t *player);
  void (* pb_seek) (struct player_t *player, int value);
  int (* get_volume) (struct player_t *player);
  void (* set_volume) (struct player_t *player, int value);
};

void mrl_list_free (struct mrl_t *mrl);

struct mrl_properties_audio_t *mrl_properties_audio_new (void);
void mrl_properties_audio_free (struct mrl_properties_audio_t *audio);
struct mrl_properties_video_t *mrl_properties_video_new (void);
void mrl_properties_video_free (struct mrl_properties_video_t *video);
struct mrl_properties_t *mrl_properties_new (void);
void mrl_properties_free (struct mrl_properties_t *prop);
struct mrl_metadata_t *mrl_metadata_new (void);
void mrl_metadata_free (struct mrl_metadata_t *meta);

#endif /* _PLAYER_INTERNALS_H_ */
