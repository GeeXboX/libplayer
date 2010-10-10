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

#ifndef SUPERVISOR_H
#define SUPERVISOR_H

typedef struct supervisor_s supervisor_t;

typedef enum supervisor_status {
  SUPERVISOR_STATUS_ERROR,
  SUPERVISOR_STATUS_OK,
} supervisor_status_t;

typedef enum supervisor_mode {
  SV_MODE_NO_WAIT,
  SV_MODE_WAIT_FOR_END,
} supervisor_mode_t;

typedef enum supervisor_ctl {
  SV_FUNC_KILL = -1,
  SV_FUNC_NOP  =  0,

  /* MRL */
  SV_FUNC_MRL_FREE,
  SV_FUNC_MRL_GET_PROPERTY,
  SV_FUNC_MRL_GET_AO_CODEC,
  SV_FUNC_MRL_GET_VO_CODEC,
  SV_FUNC_MRL_GET_SIZE,
  SV_FUNC_MRL_GET_METADATA,
  SV_FUNC_MRL_GET_METADATA_CD_TRACK,
  SV_FUNC_MRL_GET_METADATA_CD,
  SV_FUNC_MRL_GET_METADATA_DVD_TITLE,
  SV_FUNC_MRL_GET_METADATA_DVD,
  SV_FUNC_MRL_GET_METADATA_SUBTITLE,
  SV_FUNC_MRL_GET_METADATA_SUBTITLE_NB,
  SV_FUNC_MRL_GET_METADATA_AUDIO,
  SV_FUNC_MRL_GET_METADATA_AUDIO_NB,
  SV_FUNC_MRL_GET_TYPE,
  SV_FUNC_MRL_GET_RESOURCE,
  SV_FUNC_MRL_ADD_SUBTITLE,
  SV_FUNC_MRL_NEW,
  SV_FUNC_MRL_VIDEO_SNAPSHOT,

  /* Player (Un)Initialization */
  SV_FUNC_PLAYER_INIT,
  SV_FUNC_PLAYER_UNINIT,
  SV_FUNC_PLAYER_SET_VERBOSITY,

  /* Player to MRL connection */
  SV_FUNC_PLAYER_MRL_GET_CURRENT,
  SV_FUNC_PLAYER_MRL_SET,
  SV_FUNC_PLAYER_MRL_APPEND,
  SV_FUNC_PLAYER_MRL_REMOVE,
  SV_FUNC_PLAYER_MRL_REMOVE_ALL,
  SV_FUNC_PLAYER_MRL_PREVIOUS,
  SV_FUNC_PLAYER_MRL_NEXT,
  SV_FUNC_PLAYER_MRL_NEXT_PLAY,

  /* Player tuning & properties */
  SV_FUNC_PLAYER_GET_TIME_POS,
  SV_FUNC_PLAYER_GET_PERCENT_POS,
  SV_FUNC_PLAYER_SET_PLAYBACK,
  SV_FUNC_PLAYER_SET_LOOP,
  SV_FUNC_PLAYER_SET_SHUFFLE,
  SV_FUNC_PLAYER_SET_FRAMEDROP,
  SV_FUNC_PLAYER_SET_MOUSE_POS,
  SV_FUNC_PLAYER_X_WINDOW_SET_PROPS,
  SV_FUNC_PLAYER_OSD_SHOW_TEXT,
  SV_FUNC_PLAYER_OSD_STATE,

  /* Playback related controls */
  SV_FUNC_PLAYER_PB_GET_STATE,
  SV_FUNC_PLAYER_PB_START,
  SV_FUNC_PLAYER_PB_STOP,
  SV_FUNC_PLAYER_PB_PAUSE,
  SV_FUNC_PLAYER_PB_SEEK,
  SV_FUNC_PLAYER_PB_SEEK_CHAPTER,
  SV_FUNC_PLAYER_PB_SPEED,

  /* Audio related controls */
  SV_FUNC_PLAYER_AO_VOLUME_GET,
  SV_FUNC_PLAYER_AO_VOLUME_SET,
  SV_FUNC_PLAYER_AO_MUTE_GET,
  SV_FUNC_PLAYER_AO_MUTE_SET,
  SV_FUNC_PLAYER_AO_SET_DELAY,
  SV_FUNC_PLAYER_AO_SELECT,
  SV_FUNC_PLAYER_AO_PREV,
  SV_FUNC_PLAYER_AO_NEXT,

  /* Video related controls */
  SV_FUNC_PLAYER_VO_SET_ASPECT,
  SV_FUNC_PLAYER_VO_SET_PANSCAN,
  SV_FUNC_PLAYER_VO_SET_AR,

  /* Subtitles related controls */
  SV_FUNC_PLAYER_SUB_SET_DELAY,
  SV_FUNC_PLAYER_SUB_SET_ALIGN,
  SV_FUNC_PLAYER_SUB_SET_POS,
  SV_FUNC_PLAYER_SUB_SET_VIS,
  SV_FUNC_PLAYER_SUB_SCALE,
  SV_FUNC_PLAYER_SUB_SELECT,
  SV_FUNC_PLAYER_SUB_PREV,
  SV_FUNC_PLAYER_SUB_NEXT,

  /* DVD specific controls */
  SV_FUNC_PLAYER_DVD_NAV,
  SV_FUNC_PLAYER_DVD_ANGLE_SELECT,
  SV_FUNC_PLAYER_DVD_ANGLE_PREV,
  SV_FUNC_PLAYER_DVD_ANGLE_NEXT,
  SV_FUNC_PLAYER_DVD_TITLE_SELECT,
  SV_FUNC_PLAYER_DVD_TITLE_PREV,
  SV_FUNC_PLAYER_DVD_TITLE_NEXT,

  /* TV/DVB specific controls */
  SV_FUNC_PLAYER_TV_CHAN_SELECT,
  SV_FUNC_PLAYER_TV_CHAN_PREV,
  SV_FUNC_PLAYER_TV_CHAN_NEXT,

  /* Radio specific controls */
  SV_FUNC_PLAYER_RADIO_CHAN_SELECT,
  SV_FUNC_PLAYER_RADIO_CHAN_PREV,
  SV_FUNC_PLAYER_RADIO_CHAN_NEXT,

  /* VDR specific controls */
  SV_FUNC_PLAYER_VDR,

} supervisor_ctl_t;

typedef struct supervisor_data_mode_s {
  int value;
  int mode;
} supervisor_data_mode_t;

typedef struct supervisor_data_vo_s {
  int list;
  int8_t value;
  int mode;
} supervisor_data_vo_t;

typedef struct supervisor_data_args_s {
  int res;
  void *args;
} supervisor_data_args_t;

typedef struct supervisor_data_mrl_s {
  mrl_t *mrl;
  int value;
} supervisor_data_mrl_t;

typedef struct supervisor_data_sub_s {
  mrl_t *mrl;
  char *sub;
} supervisor_data_sub_t;

typedef struct supervisor_data_out_metadata_cd_s {
  char *name;
  uint32_t length;
} supervisor_data_out_metadata_cd_t;

typedef struct supervisor_data_in_metadata_dvd_s {
  mrl_t *mrl;
  int id, type;
} supervisor_data_in_metadata_dvd_t;

typedef struct supervisor_data_out_metadata_dvd_s {
  char *volumeid;
  uint8_t titles;
} supervisor_data_out_metadata_dvd_t;

typedef struct supervisor_data_out_metadata_s {
  char *name;
  char *lang;
  uint32_t id;
  int ret;
} supervisor_data_out_metadata_t;

typedef struct supervisor_data_window_s {
  int x, y;
  int w, h;
  int flags;
} supervisor_data_window_t;

typedef struct supervisor_data_snapshot_s {
  mrl_t *mrl;
  int pos;
  int type;
  const char *dst;
} supervisor_data_snapshot_t;

typedef struct supervisor_data_coord_s {
  int x;
  int y;
} supervisor_data_coord_t;

typedef struct supervisor_data_osd_s {
  const char *text;
  int x;
  int y;
  int duration;
} supervisor_data_osd_t;


supervisor_t *pl_supervisor_new (void);
supervisor_status_t pl_supervisor_init (player_t *player, int **run,
                                        pthread_t **job,
                                        pthread_cond_t **cond,
                                        pthread_mutex_t **mutex);
void pl_supervisor_uninit (player_t *player);

void pl_supervisor_send (player_t *player, supervisor_mode_t mode,
                         supervisor_ctl_t ctl, void *in, void *out);
void pl_supervisor_sync_recatch (player_t *player, pthread_t which);
void pl_supervisor_callback_in (player_t *player, pthread_t which);
void pl_supervisor_callback_out (player_t *player);

#endif /* SUPERVISOR_H */
