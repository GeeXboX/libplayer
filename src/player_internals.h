/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006-2008 Benjamin Zores <ben@geexbox.org>
 * Copyright (C) 2007-2008 Mathieu Schroeter <mathieu.schroeter@gamesover.ch>
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

#ifndef PLAYER_INTERNALS_H
#define PLAYER_INTERNALS_H

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

typedef struct mrl_metadata_cd_track_s {
  char *name;
  uint32_t length;
  struct mrl_metadata_cd_track_s *next;
} mrl_metadata_cd_track_t;

typedef struct mrl_metadata_cd_s {
  uint32_t discid;
  uint32_t tracks;
  mrl_metadata_cd_track_t *track;
} mrl_metadata_cd_t;

typedef struct mrl_metadata_s {
  char *title;
  char *artist;
  char *genre;
  char *album;
  char *year;
  char *track;
  char *comment;
  void *priv; /* private metadata, depending on resource type */
} mrl_metadata_t;

typedef struct mrl_properties_audio_s {
  char *codec;
  uint32_t bitrate;
  uint32_t bits;
  uint32_t channels;
  uint32_t samplerate;
} mrl_properties_audio_t;

typedef struct mrl_properties_video_s {
  char *codec;
  uint32_t bitrate;
  uint32_t width;
  uint32_t height;
  uint32_t aspect;          /* *10000 */
  uint32_t channels;
  uint32_t streams;
  uint32_t frameduration;   /* 1/90000 sec */
} mrl_properties_video_t;

typedef struct mrl_properties_s {
  off_t size;
  uint32_t seekable;
  uint32_t length;
  mrl_properties_audio_t *audio;
  mrl_properties_video_t *video;
} mrl_properties_t;

struct mrl_s {
  char **subs;
  mrl_type_t type;
  mrl_resource_t resource;
  mrl_properties_t *prop;
  mrl_metadata_t *meta;
  void *priv; /* private data, depending on resource type */

  /* for playlist management */
  struct mrl_s *prev;
  struct mrl_s *next;
};

typedef struct player_funcs_s {
  /* Player (Un)Init */
  init_status_t (* init) (player_t *player);
  void (* uninit) (player_t *player);
  void (* set_verbosity) (player_t *player, player_verbosity_level_t level);

  /* MRLs */
  int (* mrl_supported_res) (player_t *player, mrl_resource_t res);
  void (* mrl_retrieve_props) (player_t *player, mrl_t *mrl);
  void (* mrl_retrieve_meta) (player_t *player, mrl_t *mrl);

  /* Player properties */
  int (* get_time_pos) (player_t *player);
  void (* set_framedrop) (player_t *player, player_framedrop_t fd);

  /* Playback */
  playback_status_t (* pb_start) (player_t *player);
  void (* pb_stop) (player_t *player);
  playback_status_t (* pb_pause) (player_t *player);
  void (* pb_seek) (player_t *player, int value, player_pb_seek_t seek);
  void (* pb_seek_chapter) (player_t *player, int value, int absolute);
  void (* pb_set_speed) (player_t *player, float value);

  /* Audio */
  int (* get_volume) (player_t *player);
  void (* set_volume) (player_t *player, int value);
  player_mute_t (* get_mute) (player_t *player);
  void (* set_mute) (player_t *player, player_mute_t value);
  void (* audio_set_delay) (player_t *player, int value, int absolute);
  void (* audio_select) (player_t *player, int audio_id);
  void (* audio_prev) (player_t *player);
  void (* audio_next) (player_t *player);

  /* Video */
  void (* video_set_fs) (player_t *player, int value);
  void (* video_set_aspect) (player_t *player, player_video_aspect_t aspect,
                             int8_t value, int absolute);
  void (* video_set_panscan) (player_t *player, int8_t value, int absolute);
  void (* video_set_ar) (player_t *player, float value);

  /* Subtitles */
  void (* set_sub_delay) (player_t *player, int value);
  void (* set_sub_alignment) (player_t *player, player_sub_alignment_t a);
  void (* set_sub_pos) (player_t *player, int value);
  void (* set_sub_visibility) (player_t *player, int value);
  void (* sub_scale) (player_t *player, int value, int absolute);
  void (* sub_select) (player_t *player, int sub_id);
  void (* sub_prev) (player_t *player);
  void (* sub_next) (player_t *player);

  /* DVD */
  void (* dvd_nav) (player_t *player, player_dvdnav_t value);
  void (* dvd_angle_set) (player_t *player, int angle);
  void (* dvd_angle_prev) (player_t *player);
  void (* dvd_angle_next) (player_t *player);
  void (* dvd_title_set) (player_t *player, int title);
  void (* dvd_title_prev) (player_t *player);
  void (* dvd_title_next) (player_t *player);

  /* TV */
  void (* tv_channel_set) (player_t *player, int channel);
  void (* tv_channel_prev) (player_t *player);
  void (* tv_channel_next) (player_t *player);

  /* Radio */
  void (* radio_channel_set) (player_t *player, int channel);
  void (* radio_channel_prev) (player_t *player);
  void (* radio_channel_next) (player_t *player);

} player_funcs_t;

struct player_s {
  player_type_t type;   /* the type of player we'll use */
  player_verbosity_level_t verbosity;
  mrl_t *mrl;    /* current MRL */
  player_state_t state; /* state of the playback */
  player_loop_t loop_mode; /* loop an element or the playlist */
  int loop;             /* how many loops */
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
mrl_metadata_t *mrl_metadata_new (mrl_resource_t res);
void mrl_metadata_free (mrl_metadata_t *meta, mrl_resource_t res);
mrl_metadata_cd_track_t *mrl_metadata_cd_track_new (void);
void mrl_metadata_cd_track_append (mrl_metadata_cd_t *cd,
                                   mrl_metadata_cd_track_t *track);

int mrl_uses_vo (mrl_t *mrl);
int mrl_uses_ao (mrl_t *mrl);

#endif /* PLAYER_INTERNALS_H */
