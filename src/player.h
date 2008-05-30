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

#ifndef PLAYER_H_
#define PLAYER_H_

#include <inttypes.h>

#define LIBPLAYER_VERSION_MAJOR 0
#define LIBPLAYER_VERSION_MINOR 0
#define LIBPLAYER_VERSION_MICRO 1
#define LIBPLAYER_VERSION "0.0.1"

/* opaque data type */
typedef struct player_s player_t;
typedef struct mrl_metadata_s mrl_metadata_t;

typedef enum {
  PLAYER_MSG_NONE,          /* no error messages */
  PLAYER_MSG_INFO,          /* working operations */
  PLAYER_MSG_WARNING,       /* harmless failures */
  PLAYER_MSG_ERROR,         /* may result in hazardous behavior */
  PLAYER_MSG_CRITICAL,      /* prevents lib from working */
} player_verbosity_level_t;

typedef enum player_type {
  PLAYER_TYPE_XINE,
  PLAYER_TYPE_MPLAYER,
  PLAYER_TYPE_VLC,
  PLAYER_TYPE_GSTREAMER,
  PLAYER_TYPE_DUMMY
} player_type_t;

typedef enum player_vo {
  PLAYER_VO_NULL,
  PLAYER_VO_AUTO,
  PLAYER_VO_X11,
  PLAYER_VO_X11_SDL,
  PLAYER_VO_XV,
  PLAYER_VO_GL,
  PLAYER_VO_FB
} player_vo_t;

typedef enum player_ao {
  PLAYER_AO_NULL,
  PLAYER_AO_AUTO,
  PLAYER_AO_ALSA,
  PLAYER_AO_OSS
} player_ao_t;

typedef enum player_mrl_type {
  PLAYER_MRL_TYPE_UNKNOWN,
  PLAYER_MRL_TYPE_AUDIO,
  PLAYER_MRL_TYPE_VIDEO,
  PLAYER_MRL_TYPE_IMAGE,
} player_mrl_type_t;

typedef enum player_mrl_resource {
  PLAYER_MRL_RESOURCE_UNKNOWN,
  PLAYER_MRL_RESOURCE_CDDA,
  PLAYER_MRL_RESOURCE_CDDB,
  PLAYER_MRL_RESOURCE_DVB,
  PLAYER_MRL_RESOURCE_DVD,
  PLAYER_MRL_RESOURCE_DVDNAV,
  PLAYER_MRL_RESOURCE_FIFO,
  PLAYER_MRL_RESOURCE_FILE,
  PLAYER_MRL_RESOURCE_FTP,
  PLAYER_MRL_RESOURCE_HTTP,
  PLAYER_MRL_RESOURCE_MMS,
  PLAYER_MRL_RESOURCE_RADIO,
  PLAYER_MRL_RESOURCE_RTP,
  PLAYER_MRL_RESOURCE_RTSP,
  PLAYER_MRL_RESOURCE_SMB,
  PLAYER_MRL_RESOURCE_STDIN,
  PLAYER_MRL_RESOURCE_TCP,
  PLAYER_MRL_RESOURCE_TV,
  PLAYER_MRL_RESOURCE_UDP,
  PLAYER_MRL_RESOURCE_VCD,
} player_mrl_resource_t;

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

typedef enum player_dvdnav {
  PLAYER_DVDNAV_UP,
  PLAYER_DVDNAV_DOWN,
  PLAYER_DVDNAV_RIGHT,
  PLAYER_DVDNAV_LEFT,
  PLAYER_DVDNAV_MENU,
  PLAYER_DVDNAV_SELECT
} player_dvdnav_t;

typedef enum player_metadata {
  PLAYER_METADATA_TITLE,
  PLAYER_METADATA_ARTIST,
  PLAYER_METADATA_GENRE,
  PLAYER_METADATA_ALBUM,
  PLAYER_METADATA_YEAR,
  PLAYER_METADATA_TRACK,
  PLAYER_METADATA_COMMENT,
} player_metadata_t;

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
  uint32_t aspect;
  uint32_t channels;
  uint32_t streams;
  uint32_t framerate;
} mrl_properties_video_t;

typedef struct mrl_properties_s {
  off_t size;
  uint32_t seekable;
  uint32_t length;
  mrl_properties_audio_t *audio;
  mrl_properties_video_t *video;
} mrl_properties_t;

typedef struct mrl_s {
  char *name;
  char *subtitle;
  char *cover;
  player_mrl_type_t type;
  player_mrl_resource_t resource;
  mrl_properties_t *prop;
  mrl_metadata_t *meta;

  /* for playlist management */
  struct mrl_s *prev;
  struct mrl_s *next;
} mrl_t;


/* player init/uninit prototypes */
player_t *player_init (player_type_t type, player_ao_t ao, player_vo_t vo,
                       player_verbosity_level_t verbosity,
                       int event_cb (player_event_t e, void *data));
void player_uninit (player_t *player);
void player_set_verbosity (player_t *player, player_verbosity_level_t level);

/* MRL helpers */
mrl_t *mrl_new (player_t *player, char *name, char *subtitle);
mrl_t *player_get_mrl (player_t *player);
void mrl_free (mrl_t *mrl, int recursive);
void mrl_list_free (mrl_t *mrl);

void player_mrl_set (player_t *player, char *location, char *subtitle);
void player_mrl_append (player_t *player,
                        char *location, char *subtitle, player_add_mrl_t when);
void player_mrl_remove (player_t *player);
void player_mrl_remove_all (player_t *player);
void player_mrl_previous (player_t *player);
void player_mrl_next (player_t *player);
void player_mrl_retrieve_properties (player_t *player, mrl_t *mrl);
char *player_mrl_get_metadata (player_t *player, mrl_t *mrl, player_metadata_t m);

/* get player playback properties */
int player_get_volume (player_t *player);
player_mute_t player_get_mute (player_t *player);
int player_get_time_pos (player_t *player);

/* tune player playback properties */
void player_set_loop (player_t *player, int value);
void player_set_shuffle (player_t *player, int value);
void player_set_volume (player_t *player, int value);
void player_set_mute (player_t *player, player_mute_t value);
void player_set_sub_delay (player_t *player, float value);

/* player controls */
void player_playback_start (player_t *player);
void player_playback_stop (player_t *player);
void player_playback_pause (player_t *player);
void player_playback_seek (player_t *player, int value);
void player_playback_dvdnav (player_t *player, player_dvdnav_t value);

#endif /* PLAYER_H_ */
