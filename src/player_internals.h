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

#ifndef PLAYER_INTERNALS_H_
#define PLAYER_INTERNALS_H_

typedef enum init_status {
  PLAYER_INIT_OK,
  PLAYER_INIT_ERROR
} init_status_t;

typedef enum playback_status {
  PLAYER_PB_OK,
  PLAYER_PB_FATAL,
  PLAYER_PB_ERROR
} playback_status_t;

typedef struct player_funcs_s {
  init_status_t (* init) (player_t *player);
  void (* uninit) (player_t *player);
  void (* mrl_get_props) (player_t *player);
  void (* mrl_get_meta) (player_t *player);
  playback_status_t (* pb_start) (player_t *player);
  void (* pb_stop) (player_t *player);
  playback_status_t (* pb_pause) (player_t *player);
  void (* pb_seek) (player_t *player, int value);
  void (* pb_dvdnav) (player_t *player, player_dvdnav_t value);
  int (* get_volume) (player_t *player);
  player_mute_t (* get_mute) (player_t *player);
  void (* set_volume) (player_t *player, int value);
  void (* set_mute) (player_t *player, player_mute_t value);
  void (* set_sub_delay) (player_t *player, float value);
} player_funcs_t;

void mrl_list_free (mrl_t *mrl);

mrl_properties_audio_t *mrl_properties_audio_new (void);
void mrl_properties_audio_free (mrl_properties_audio_t *audio);
mrl_properties_video_t *mrl_properties_video_new (void);
void mrl_properties_video_free (mrl_properties_video_t *video);
mrl_properties_t *mrl_properties_new (void);
void mrl_properties_free (mrl_properties_t *prop);
mrl_metadata_t *mrl_metadata_new (void);
void mrl_metadata_free (mrl_metadata_t *meta);

int mrl_uses_vo (mrl_t *mrl);
int mrl_uses_ao (mrl_t *mrl);

#endif /* PLAYER_INTERNALS_H_ */
