/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006-2008 Benjamin Zores <ben@geexbox.org>
 * Copyright (C) 2007-2008 Mathieu Schroeter <mathieu@schroetersa.ch>
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

#ifndef PLAYER_INTERNALS_H
#define PLAYER_INTERNALS_H

#include <pthread.h>

struct playlist_s;
struct event_handler_s;
struct supervisor_s;

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

typedef struct mrl_metadata_dvd_title_s {
  uint32_t chapters;
  uint32_t angles;
  uint32_t length;
  struct mrl_metadata_dvd_title_s *next;
} mrl_metadata_dvd_title_t;

typedef struct mrl_metadata_dvd_s {
  char *volumeid;
  uint8_t titles;
  mrl_metadata_dvd_title_t *title;
} mrl_metadata_dvd_t;

typedef struct mrl_metadata_sub_s {
  char *name;
  char *lang;
  uint32_t id;
  struct mrl_metadata_sub_s *next;
} mrl_metadata_sub_t;

typedef struct mrl_metadata_audio_s {
  char *name;
  char *lang;
  uint32_t id;
  struct mrl_metadata_audio_s *next;
} mrl_metadata_audio_t;

typedef struct mrl_metadata_s {
  char *title;
  char *artist;
  char *genre;
  char *album;
  char *year;
  char *track;
  char *comment;
  mrl_metadata_sub_t *subs;
  mrl_metadata_audio_t *audio_streams;
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
  init_status_t (*init) (player_t *player);
  void (*uninit) (player_t *player);
  void (*set_verbosity) (player_t *player, player_verbosity_level_t level);

  /* MRLs */
  void (*mrl_retrieve_props) (player_t *player, mrl_t *mrl);
  void (*mrl_retrieve_meta) (player_t *player, mrl_t *mrl);
  void (*mrl_video_snapshot) (player_t *player, mrl_t *mrl,
                              int pos, mrl_snapshot_t t, const char *dst);

  /* Player properties */
  int (*get_time_pos) (player_t *player);
  int (*get_percent_pos) (player_t *player);
  void (*set_framedrop) (player_t *player, player_framedrop_t fd);
  void (*set_mouse_pos) (player_t *player, int x, int y);
  void (*osd_show_text) (player_t *player,
                         const char *text, int x, int y, int duration);
  void (*osd_state) (player_t *player, int value);

  /* Playback */
  playback_status_t (*pb_start) (player_t *player);
  void (*pb_stop) (player_t *player);
  playback_status_t (*pb_pause) (player_t *player);
  void (*pb_seek) (player_t *player, int value, player_pb_seek_t seek);
  void (*pb_seek_chapter) (player_t *player, int value, int absolute);
  void (*pb_set_speed) (player_t *player, float value);

  /* Audio */
  int (*audio_get_volume) (player_t *player);
  void (*audio_set_volume) (player_t *player, int value);
  player_mute_t (*audio_get_mute) (player_t *player);
  void (*audio_set_mute) (player_t *player, player_mute_t value);
  void (*audio_set_delay) (player_t *player, int value, int absolute);
  void (*audio_select) (player_t *player, int audio_id);
  void (*audio_prev) (player_t *player);
  void (*audio_next) (player_t *player);

  /* Video */
  void (*video_set_aspect) (player_t *player, player_video_aspect_t aspect,
                            int8_t value, int absolute);
  void (*video_set_panscan) (player_t *player, int8_t value, int absolute);
  void (*video_set_ar) (player_t *player, float value);

  /* Subtitles */
  void (*sub_set_delay) (player_t *player, int value);
  void (*sub_set_alignment) (player_t *player, player_sub_alignment_t a);
  void (*sub_set_pos) (player_t *player, int value);
  void (*sub_set_visibility) (player_t *player, int value);
  void (*sub_scale) (player_t *player, int value, int absolute);
  void (*sub_select) (player_t *player, int sub_id);
  void (*sub_prev) (player_t *player);
  void (*sub_next) (player_t *player);

  /* DVD */
  void (*dvd_nav) (player_t *player, player_dvdnav_t value);
  void (*dvd_angle_set) (player_t *player, int angle);
  void (*dvd_angle_prev) (player_t *player);
  void (*dvd_angle_next) (player_t *player);
  void (*dvd_title_set) (player_t *player, int title);
  void (*dvd_title_prev) (player_t *player);
  void (*dvd_title_next) (player_t *player);

  /* TV */
  void (*tv_channel_set) (player_t *player, const char *channel);
  void (*tv_channel_prev) (player_t *player);
  void (*tv_channel_next) (player_t *player);

  /* Radio */
  void (*radio_channel_set) (player_t *player, const char *channel);
  void (*radio_channel_prev) (player_t *player);
  void (*radio_channel_next) (player_t *player);

  /* VDR */
  void (*vdr) (player_t *player, player_vdr_t value);

} player_funcs_t;

#define PL_NOT_SUPPORTED NULL

#define PLAYER_FUNCS_WARN(fct) \
  pl_log (player, PLAYER_MSG_WARNING, MODULE_NAME, #fct " is unimplemented")

#define PLAYER_FUNCS(fct, arg...)       \
  if (player->funcs->fct)               \
    player->funcs->fct (player, ##arg); \
  else                                  \
    PLAYER_FUNCS_WARN (fct);

#define PLAYER_FUNCS_RES(fct, res, arg...)    \
  if (player->funcs->fct)                     \
    res = player->funcs->fct (player, ##arg); \
  else                                        \
    PLAYER_FUNCS_WARN (fct);

struct player_s {
  player_type_t type;         /* the type of player we'll use */
  player_verbosity_level_t verbosity;
  pthread_mutex_t mutex_verb;

  struct playlist_s *playlist;

  player_state_t state;       /* state of the playback        */
  player_pb_t pb_mode;        /* mode of the playback         */

  player_ao_t   ao;           /* audio output driver name     */
  player_vo_t   vo;           /* video output driver name     */

  struct window_s *window;    /* window backend               */
  const char   *x11_display;  /* for non-default display      */
  uint32_t      winid;        /* embedded Window ID for X11   */
  int   x, y;                 /* video position               */
  int   w, h;                 /* video size                   */
  float aspect;               /* video aspect                 */

  player_quality_level_t quality; /* picture decoding quality */

  struct supervisor_s    *supervisor; /* manage all public operations        */
  struct event_handler_s *event;      /* event handler                       */
  int (*event_cb) (player_event_t e, void *data); /* frontend event callback */
  void *user_data;                  /* user data for frontend event callback */

  struct player_funcs_s *funcs; /* bindings to player specific functions     */
  void *priv;                   /* specific conf related to the player type  */
};

#define ARRAY_NB_ELEMENTS(array) (sizeof (array) / sizeof (array[0]))

#define MRL_USES_VO(m) ((m) && (m)->prop && (m)->prop->video)
#define MRL_USES_AO(m) ((m) && (m)->prop && (m)->prop->audio)

#define PFREE(p) do { if (p) free (p); p = NULL; } while (0)
#define PCALLOC(s, n) calloc (n, sizeof (s))

/*****************************************************************************/
/*                          MRL Internal functions                           */
/*****************************************************************************/

mrl_properties_audio_t *mrl_properties_audio_new (void);
void mrl_properties_audio_free (mrl_properties_audio_t *audio);
mrl_properties_video_t *mrl_properties_video_new (void);
void mrl_properties_video_free (mrl_properties_video_t *video);
mrl_properties_t *mrl_properties_new (void);
void mrl_properties_free (mrl_properties_t *prop);
mrl_metadata_t *mrl_metadata_new (mrl_resource_t res);
void mrl_metadata_free (mrl_metadata_t *meta, mrl_resource_t res);
mrl_metadata_cd_track_t *mrl_metadata_cd_get_track (mrl_metadata_cd_t *cd,
                                                    uint32_t id);
mrl_metadata_cd_track_t *mrl_metadata_cd_track_new (void);
mrl_metadata_dvd_title_t *mrl_metadata_dvd_get_title (mrl_metadata_dvd_t *dvd,
                                                      uint32_t id);
mrl_metadata_dvd_title_t *mrl_metadata_dvd_title_new (void);
mrl_metadata_sub_t *mrl_metadata_sub_get (mrl_metadata_sub_t **sub, uint32_t id);
mrl_metadata_audio_t *mrl_metadata_audio_get (mrl_metadata_audio_t **audio,
                                              uint32_t id);
void mrl_list_free (mrl_t *mrl);

/*****************************************************************************/
/*                   MRL Internal (Supervisor) functions                     */
/*****************************************************************************/

void mrl_sv_free (mrl_t *mrl, int recursive);
uint32_t mrl_sv_get_property (player_t *player,
                              mrl_t *mrl, mrl_properties_type_t p);
char *mrl_sv_get_audio_codec (player_t *player, mrl_t *mrl);
char *mrl_sv_get_video_codec (player_t *player, mrl_t *mrl);
off_t mrl_sv_get_size (player_t *player, mrl_t *mrl);
char *mrl_sv_get_metadata (player_t *player, mrl_t *mrl, mrl_metadata_type_t m);
char *mrl_sv_get_metadata_cd_track (player_t *player,
                                    mrl_t *mrl, int trackid, uint32_t *length);
uint32_t mrl_sv_get_metadata_cd (player_t *player,
                                 mrl_t *mrl, mrl_metadata_cd_type_t m);
uint32_t mrl_sv_get_metadata_dvd_title (player_t *player, mrl_t *mrl,
                                        int titleid, mrl_metadata_dvd_type_t m);
char *mrl_sv_get_metadata_dvd (player_t *player, mrl_t *mrl, uint8_t *titles);
int mrl_sv_get_metadata_subtitle (player_t *player, mrl_t *mrl, int pos,
                                  uint32_t *id, char **name, char **lang);
uint32_t mrl_sv_get_metadata_subtitle_nb (player_t *player, mrl_t *mrl);
int mrl_sv_get_metadata_audio (player_t *player, mrl_t *mrl, int pos,
                               uint32_t *id, char **name, char **lang);
uint32_t mrl_sv_get_metadata_audio_nb (player_t *player, mrl_t *mrl);
mrl_type_t mrl_sv_get_type (player_t *player, mrl_t *mrl);
mrl_resource_t mrl_sv_get_resource (player_t *player, mrl_t *mrl);
void mrl_sv_add_subtitle (player_t *player, mrl_t *mrl, char *subtitle);
mrl_t *mrl_sv_new (player_t *player, mrl_resource_t res, void *args);
void mrl_sv_video_snapshot (player_t *player, mrl_t *mrl,
                            int pos, mrl_snapshot_t t, const char *dst);

/*****************************************************************************/
/*                 Player Internal (Supervisor) functions                    */
/*****************************************************************************/

/* Player (Un)Initialization */
init_status_t player_sv_init (player_t *player);
void player_sv_uninit (player_t *player);
void player_sv_set_verbosity (player_t *player, player_verbosity_level_t level);

/* Player to MRL connection */
mrl_t *player_sv_mrl_get_current (player_t *player);
void player_sv_mrl_set (player_t *player, mrl_t *mrl);
void player_sv_mrl_append (player_t *player, mrl_t *mrl, player_mrl_add_t when);
void player_sv_mrl_remove (player_t *player);
void player_sv_mrl_remove_all (player_t *player);
void player_sv_mrl_previous (player_t *player);
void player_sv_mrl_next (player_t *player);
void player_sv_mrl_next_play (player_t *player);

/* Player tuning & properties */
int player_sv_get_time_pos (player_t *player);
int player_sv_get_percent_pos (player_t *player);
void player_sv_set_playback (player_t *player, player_pb_t pb);
void player_sv_set_loop (player_t *player, player_loop_t loop, int value);
void player_sv_set_shuffle (player_t *player, int value);
void player_sv_set_framedrop (player_t *player, player_framedrop_t fd);
void player_sv_set_mouse_position (player_t *player, int x, int y);
void player_sv_x_window_set_properties (player_t *player,
                                        int x, int y, int w, int h, int flags);
void player_sv_osd_show_text (player_t *player,
                              const char *text, int x, int y, int duration);
void player_sv_osd_state (player_t *player, int value);

/* Playback related controls */
player_pb_state_t player_sv_playback_get_state (player_t *player);
void player_sv_playback_start (player_t *player);
void player_sv_playback_stop (player_t *player);
void player_sv_playback_pause (player_t *player);
void player_sv_playback_seek (player_t *player,
                              int value, player_pb_seek_t seek);
void player_sv_playback_seek_chapter (player_t *player,
                                      int value, int absolute);
void player_sv_playback_speed (player_t *player, float value);

/* Audio related controls */
int player_sv_audio_volume_get (player_t *player);
void player_sv_audio_volume_set (player_t *player, int value);
player_mute_t player_sv_audio_mute_get (player_t *player);
void player_sv_audio_mute_set (player_t *player, player_mute_t value);
void player_sv_audio_set_delay (player_t *player, int value, int absolute);
void player_sv_audio_select (player_t *player, int audio_id);
void player_sv_audio_prev (player_t *player);
void player_sv_audio_next (player_t *player);

/* Video related controls */
void player_sv_video_set_aspect (player_t *player, player_video_aspect_t aspect,
                                 int8_t value, int absolute);
void player_sv_video_set_panscan (player_t *player, int8_t value, int absolute);
void player_sv_video_set_aspect_ratio (player_t *player, float value);

/* Subtitles related controls */
void player_sv_subtitle_set_delay (player_t *player, int value);
void player_sv_subtitle_set_alignment (player_t *player,
                                       player_sub_alignment_t a);
void player_sv_subtitle_set_position (player_t *player, int value);
void player_sv_subtitle_set_visibility (player_t *player, int value);
void player_sv_subtitle_scale (player_t *player, int value, int absolute);
void player_sv_subtitle_select (player_t *player, int sub_id);
void player_sv_subtitle_prev (player_t *player);
void player_sv_subtitle_next (player_t *player);

/* DVD specific controls */
void player_sv_dvd_nav (player_t *player, player_dvdnav_t value);
void player_sv_dvd_angle_select (player_t *player, int angle);
void player_sv_dvd_angle_prev (player_t *player);
void player_sv_dvd_angle_next (player_t *player);
void player_sv_dvd_title_select (player_t *player, int title);
void player_sv_dvd_title_prev (player_t *player);
void player_sv_dvd_title_next (player_t *player);

/* TV/DVB specific controls */
void player_sv_tv_channel_select (player_t *player, const char *channel);
void player_sv_tv_channel_prev (player_t *player);
void player_sv_tv_channel_next (player_t *player);

/* Radio specific controls */
void player_sv_radio_channel_select (player_t *player, const char *channel);
void player_sv_radio_channel_prev (player_t *player);
void player_sv_radio_channel_next (player_t *player);

/* VDR specific controls */
void player_sv_vdr (player_t *player, player_vdr_t value);

#endif /* PLAYER_INTERNALS_H */
