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

struct player_t;
struct player_funcs_t;

typedef enum player_type_s player_type_t;
enum player_type_s {
  PLAYER_TYPE_XINE,
  PLAYER_TYPE_DUMMY
};

typedef enum player_state_s player_state_t;
enum player_state_s {
  PLAYER_STATE_IDLE,
  PLAYER_STATE_PAUSE,
  PLAYER_STATE_RUNNING
};

typedef enum player_mrl_type_s player_mrl_type_t;
enum player_mrl_type_s {
  PLAYER_MRL_TYPE_NONE,
  PLAYER_MRL_TYPE_AUDIO,
  PLAYER_MRL_TYPE_VIDEO
};

typedef enum player_add_mrl_s player_add_mrl_t;
enum player_add_mrl_s {
  PLAYER_ADD_MRL_NOW,
  PLAYER_ADD_MRL_QUEUE
};

typedef enum player_event_s player_event_t;
enum player_event_s {
  PLAYER_EVENT_UNKNOWN,
  PLAYER_EVENT_PLAYBACK_START,
  PLAYER_EVENT_PLAYBACK_STOP,
  PLAYER_EVENT_PLAYBACK_FINISHED,
  PLAYER_EVENT_MRL_UPDATED
};

struct mrl_properties_t {
  /* not yet defined */
};

struct mrl_metadata_t {
  /* not yet defined */
};

struct mrl_t {
  char *name;
  char *cover;
  player_mrl_type_t type;
  struct mrl_properties_t *prop;
  struct mrl_metadata_t *meta;

  /* for playlist management */
  struct mrl_t *prev;
  struct mrl_t *next;
};

struct player_t {
  player_type_t type;   /* the type of player we'll use */
  struct mrl_t *mrl;    /* current MRL */
  player_state_t state; /* state of the playback */
  int loop;             /* loop elements from playlist */
  int shuffle;          /* shuffle MRLs from playlist */
  const char *ao;       /* audio output driver name */
  const char *vo;       /* video output driver name */
  int x, y;             /* video position */
  int w, h;             /* video size */
  int (*event_cb) (player_event_t e, void *data); /* frontend event callback */
  struct player_funcs_t *funcs; /* bindings to player specific functions */ 
  void *priv;           /* specific configuration related to the player type */
};

/* player init/uninit prototypes */
struct player_t *player_init (player_type_t type, char *ao, char *vo,
                              int event_cb (player_event_t e, void *data));
void player_uninit (struct player_t *player);

/* MRL helpers */
void player_mrl_append (struct player_t *player,
                        char *location, player_mrl_type_t type,
                        char *cover, player_add_mrl_t when);
void player_mrl_previous (struct player_t *player);
void player_mrl_next (struct player_t *player);

/* get player playback properties */
int player_get_volume (struct player_t *player);

/* tune player playback properties */
void player_set_loop (struct player_t *player, int value);
void player_set_shuffle (struct player_t *player, int value);
void player_set_volume (struct player_t *player, int value);

/* player controls */
void player_playback_start (struct player_t *player);
void player_playback_stop (struct player_t *player);
void player_playback_pause (struct player_t *player);
void player_playback_seek (struct player_t *player, int value);

#endif /* _PLAYER_H_ */
