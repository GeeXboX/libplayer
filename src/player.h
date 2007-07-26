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

#ifndef _PLAYER_H_
#define _PLAYER_H_

#include <inttypes.h>

struct player_funcs_s;

typedef enum player_type {
  PLAYER_TYPE_XINE,
  PLAYER_TYPE_MPLAYER,
  PLAYER_TYPE_VLC,
  PLAYER_TYPE_DUMMY
} player_type_t;

typedef enum player_vo {
  PLAYER_VO_NULL,
  PLAYER_VO_X11,
  PLAYER_VO_X11_SDL,
  PLAYER_VO_XV,
  PLAYER_VO_FB
} player_vo_t;

typedef enum player_ao {
  PLAYER_AO_NULL,
  PLAYER_AO_ALSA,
  PLAYER_AO_OSS
} player_ao_t;

typedef enum player_state {
  PLAYER_STATE_IDLE,
  PLAYER_STATE_PAUSE,
  PLAYER_STATE_RUNNING
} player_state_t;

typedef enum player_mrl_type {
  PLAYER_MRL_TYPE_UNKNOWN,
  PLAYER_MRL_TYPE_AUDIO,
  PLAYER_MRL_TYPE_VIDEO,
  PLAYER_MRL_TYPE_IMAGE
} player_mrl_type_t;

typedef enum player_add_mrl {
  PLAYER_ADD_MRL_NOW,
  PLAYER_ADD_MRL_QUEUE
} player_add_mrl_t;

typedef enum player_event {
  PLAYER_EVENT_UNKNOWN,
  PLAYER_EVENT_PLAYBACK_START,
  PLAYER_EVENT_PLAYBACK_STOP,
  PLAYER_EVENT_PLAYBACK_FINISHED,
  PLAYER_EVENT_MRL_UPDATED
} player_event_t;

typedef enum player_mute {
  PLAYER_MUTE_UNKNOWN,
  PLAYER_MUTE_ON,
  PLAYER_MUTE_OFF
} player_mute_t;

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
  uint32_t channels;
  uint32_t streams;
} mrl_properties_video_t;

typedef struct mrl_properties_s {
  off_t size;
  uint32_t seekable;
  mrl_properties_audio_t *audio;
  mrl_properties_video_t *video;
} mrl_properties_t;

typedef struct mrl_metadata_s {
  char *title;
  char *artist;
  char *genre;
  char *album;
  char *year;
  char *track;
} mrl_metadata_t;

typedef struct mrl_s {
  char *name;
  char *cover;
  player_mrl_type_t type;
  mrl_properties_t *prop;
  mrl_metadata_t *meta;

  /* for playlist management */
  struct mrl_s *prev;
  struct mrl_s *next;
} mrl_t;

typedef struct player_s {
  player_type_t type;   /* the type of player we'll use */
  mrl_t *mrl;    /* current MRL */
  player_state_t state; /* state of the playback */
  int loop;             /* loop elements from playlist */
  int shuffle;          /* shuffle MRLs from playlist */
  player_ao_t ao;       /* audio output driver name */
  player_vo_t vo;       /* video output driver name */
  int x, y;             /* video position */
  int w, h;             /* video size */
  int (*event_cb) (player_event_t e, void *data); /* frontend event callback */
  struct player_funcs_s *funcs; /* bindings to player specific functions */ 
  void *priv;           /* specific configuration related to the player type */
} player_t;

/* player init/uninit prototypes */
player_t *player_init (player_type_t type, player_ao_t ao, player_vo_t vo,
                              int event_cb (player_event_t e, void *data));
void player_uninit (player_t *player);

/* MRL helpers */
mrl_t *mrl_new (char *name, player_mrl_type_t type);
void mrl_free (mrl_t *mrl, int recursive);
void mrl_list_free (mrl_t *mrl);

void player_mrl_append (player_t *player,
                        char *location, player_mrl_type_t type,
                        player_add_mrl_t when);
void player_mrl_previous (player_t *player);
void player_mrl_next (player_t *player);
void player_mrl_get_properties (player_t *player);
void player_mrl_get_metadata (player_t *player);

/* get player playback properties */
int player_get_volume (player_t *player);
player_mute_t player_get_mute (player_t *player);

/* tune player playback properties */
void player_set_loop (player_t *player, int value);
void player_set_shuffle (player_t *player, int value);
void player_set_volume (player_t *player, int value);
void player_set_mute (player_t *player, player_mute_t value);

/* player controls */
void player_playback_start (player_t *player);
void player_playback_stop (player_t *player);
void player_playback_pause (player_t *player);
void player_playback_seek (player_t *player, int value);

#endif /* _PLAYER_H_ */
