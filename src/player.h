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

#ifndef _PLAYER_H_
#define _PLAYER_H_

struct player_funcs_s;

typedef enum player_type {
  PLAYER_TYPE_XINE,
  PLAYER_TYPE_DUMMY
} player_type_t;

typedef enum player_state {
  PLAYER_STATE_IDLE,
  PLAYER_STATE_PAUSE,
  PLAYER_STATE_RUNNING
} player_state_t;

typedef enum player_mrl_type {
  PLAYER_MRL_TYPE_NONE,
  PLAYER_MRL_TYPE_AUDIO,
  PLAYER_MRL_TYPE_VIDEO
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
  const char *ao;       /* audio output driver name */
  const char *vo;       /* video output driver name */
  int x, y;             /* video position */
  int w, h;             /* video size */
  int (*event_cb) (player_event_t e, void *data); /* frontend event callback */
  struct player_funcs_s *funcs; /* bindings to player specific functions */ 
  void *priv;           /* specific configuration related to the player type */
} player_t;

/* player init/uninit prototypes */
player_t *player_init (player_type_t type, char *ao, char *vo,
                              int event_cb (player_event_t e, void *data));
void player_uninit (player_t *player);

/* MRL helpers */
void player_mrl_append (player_t *player,
                        char *location, player_mrl_type_t type,
                        char *cover, player_add_mrl_t when);
void player_mrl_previous (player_t *player);
void player_mrl_next (player_t *player);
void player_mrl_get_properties (player_t *player, mrl_t *mrl);
void player_mrl_get_metadata (player_t *player, mrl_t *mrl);

/* get player playback properties */
int player_get_volume (player_t *player);

/* tune player playback properties */
void player_set_loop (player_t *player, int value);
void player_set_shuffle (player_t *player, int value);
void player_set_volume (player_t *player, int value);

/* player controls */
void player_playback_start (player_t *player);
void player_playback_stop (player_t *player);
void player_playback_pause (player_t *player);
void player_playback_seek (player_t *player, int value);

#endif /* _PLAYER_H_ */
