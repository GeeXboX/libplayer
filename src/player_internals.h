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

struct x11_s;

typedef enum init_status {
  PLAYER_INIT_OK,
  PLAYER_INIT_ERROR
} init_status_t;

typedef enum playback_status {
  PLAYER_PB_OK,
  PLAYER_PB_FATAL,
  PLAYER_PB_ERROR
} playback_status_t;

typedef enum player_state {
  PLAYER_STATE_IDLE,
  PLAYER_STATE_PAUSE,
  PLAYER_STATE_RUNNING
} player_state_t;

typedef enum identify_flags {
  IDENTIFY_AUDIO      = (1 << 0),
  IDENTIFY_VIDEO      = (1 << 1),
  IDENTIFY_METADATA   = (1 << 2),
  IDENTIFY_PROPERTIES = (1 << 3),
} identify_flags_t;

struct mrl_metadata_s {
  char *title;
  char *artist;
  char *genre;
  char *album;
  char *year;
  char *track;
  char *comment;
};

struct mrl_properties_audio_s {
  char *codec;
  uint32_t bitrate;
  uint32_t bits;
  uint32_t channels;
  uint32_t samplerate;
};

struct mrl_properties_video_s {
  char *codec;
  uint32_t bitrate;
  uint32_t width;
  uint32_t height;
  uint32_t aspect;          /* *10000 */
  uint32_t channels;
  uint32_t streams;
  uint32_t frameduration;   /* 1/90000 sec */
};

typedef struct player_funcs_s {
  init_status_t (* init) (player_t *player);
  void (* uninit) (player_t *player);
  void (* set_verbosity) (player_t *player, player_verbosity_level_t level);
  void (* mrl_retrieve_props) (player_t *player, mrl_t *mrl);
  void (* mrl_retrieve_meta) (player_t *player, mrl_t *mrl);
  playback_status_t (* pb_start) (player_t *player);
  void (* pb_stop) (player_t *player);
  playback_status_t (* pb_pause) (player_t *player);
  void (* pb_seek) (player_t *player, int value);
  void (* pb_dvdnav) (player_t *player, player_dvdnav_t value);
  int (* get_volume) (player_t *player);
  player_mute_t (* get_mute) (player_t *player);
  int (* get_time_pos) (player_t *player);
  void (* set_volume) (player_t *player, int value);
  void (* set_mute) (player_t *player, player_mute_t value);
  void (* set_sub_delay) (player_t *player, float value);
} player_funcs_t;

struct player_s {
  player_type_t type;   /* the type of player we'll use */
  player_verbosity_level_t verbosity;
  mrl_t *mrl;    /* current MRL */
  player_state_t state; /* state of the playback */
  int loop;             /* loop elements from playlist */
  int shuffle;          /* shuffle MRLs from playlist */
  player_ao_t ao;       /* audio output driver name */
  player_vo_t vo;       /* video output driver name */
  int x, y;             /* video position */
  int w, h;             /* video size */
  float aspect;         /* video aspect */
  struct x11_s *x11;    /* for X11 video out */
  int (*event_cb) (player_event_t e, void *data); /* frontend event callback */
  struct player_funcs_s *funcs; /* bindings to player specific functions */ 
  void *priv;           /* specific configuration related to the player type */
};

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
