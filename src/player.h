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

#ifndef PLAYER_H
#define PLAYER_H

#ifdef __cplusplus
extern "C" {
#if 0 /* avoid EMACS indent */
}
#endif /* 0 */
#endif /* __cplusplus */

#include <inttypes.h>

#define LIBPLAYER_VERSION_MAJOR 0
#define LIBPLAYER_VERSION_MINOR 0
#define LIBPLAYER_VERSION_MICRO 1
#define LIBPLAYER_VERSION "0.0.1"

/***************************************************************************/
/*                                                                         */
/* Player (Un)Initialization                                               */
/*  Mandatory for all operations below                                     */
/*                                                                         */
/***************************************************************************/

typedef struct player_s player_t;

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

typedef enum player_event {
  PLAYER_EVENT_UNKNOWN,
  PLAYER_EVENT_PLAYBACK_START,
  PLAYER_EVENT_PLAYBACK_STOP,
  PLAYER_EVENT_PLAYBACK_FINISHED,
  PLAYER_EVENT_MRL_UPDATED
} player_event_t;

typedef enum {
  PLAYER_MSG_NONE,          /* no error messages */
  PLAYER_MSG_INFO,          /* working operations */
  PLAYER_MSG_WARNING,       /* harmless failures */
  PLAYER_MSG_ERROR,         /* may result in hazardous behavior */
  PLAYER_MSG_CRITICAL,      /* prevents lib from working */
} player_verbosity_level_t;

player_t *player_init (player_type_t type, player_ao_t ao, player_vo_t vo,
                       player_verbosity_level_t verbosity,
                       int event_cb (player_event_t e, void *data));
void player_uninit (player_t *player);
void player_set_verbosity (player_t *player, player_verbosity_level_t level);

/***************************************************************************/
/*                                                                         */
/* Media Resource Locater (MRL) Helpers                                    */
/*  MRLs can have multiple types and are used to define a stream           */
/*                                                                         */
/***************************************************************************/

typedef struct mrl_s mrl_t;

typedef enum mrl_type {
  MRL_TYPE_UNKNOWN,
  MRL_TYPE_AUDIO,
  MRL_TYPE_VIDEO,
  MRL_TYPE_IMAGE,
} mrl_type_t;

typedef enum mrl_resource {
  MRL_RESOURCE_UNKNOWN,

  /* Local Streams */
  MRL_RESOURCE_FIFO,
  MRL_RESOURCE_FILE,
  MRL_RESOURCE_STDIN,

  /* Audio CD */
  MRL_RESOURCE_CDDA,
  MRL_RESOURCE_CDDB,

  /* Video discs */
  MRL_RESOURCE_DVD,
  MRL_RESOURCE_DVDNAV,
  MRL_RESOURCE_VCD,

  /* Radio/Television */
  MRL_RESOURCE_DVB,
  MRL_RESOURCE_PVR,
  MRL_RESOURCE_RADIO,
  MRL_RESOURCE_TV,

  /* Network Streams */
  MRL_RESOURCE_FTP,  
  MRL_RESOURCE_HTTP,
  MRL_RESOURCE_MMS,
  MRL_RESOURCE_RTP,
  MRL_RESOURCE_RTSP,
  MRL_RESOURCE_SMB,
  MRL_RESOURCE_TCP,
  MRL_RESOURCE_UDP,
  MRL_RESOURCE_UNSV,
} mrl_resource_t;

/* for local streams */
typedef struct mrl_resource_local_args_s {
  char *location;
  int playlist;
} mrl_resource_local_args_t;

/* for audio CD */
typedef struct mrl_resource_cd_args_s {
  char *device;
  uint8_t speed;
  uint8_t track_start;
  uint8_t track_end;
} mrl_resource_cd_args_t;

/* for video discs */ 
typedef struct mrl_resource_videodisc_args_s {
  char *device;
  uint8_t speed;
  uint8_t angle;
  uint8_t title_start;
  uint8_t title_end;
  uint8_t chapter_start;
  uint8_t chapter_end;
  uint8_t track_start;
  uint8_t track_end;
  char *audio_lang;
  char *sub_lang;
  uint8_t sub_cc;
} mrl_resource_videodisc_args_t;

/* for radio/tv streams */ 
typedef struct mrl_resource_tv_args_s {
  char *device;
  char *driver;
  uint8_t input;
  int width;
  int height;
  int fps;
  char *output_format;
  char *norm;
} mrl_resource_tv_args_t;

/* for network streams */ 
typedef struct mrl_resource_network_args_s {
  char *url;
  char *username;
  char *password;
  char *user_agent;
} mrl_resource_network_args_t;

typedef enum mrl_metadata_type {
  MRL_METADATA_TITLE,
  MRL_METADATA_ARTIST,
  MRL_METADATA_GENRE,
  MRL_METADATA_ALBUM,
  MRL_METADATA_YEAR,
  MRL_METADATA_TRACK,
  MRL_METADATA_COMMENT,
} mrl_metadata_type_t;

typedef enum mrl_properties_type {
  MRL_PROPERTY_SEEKABLE,
  MRL_PROPERTY_LENGTH,
  MRL_PROPERTY_AUDIO_BITRATE,
  MRL_PROPERTY_AUDIO_BITS,
  MRL_PROPERTY_AUDIO_CHANNELS,
  MRL_PROPERTY_AUDIO_SAMPLERATE,
  MRL_PROPERTY_VIDEO_BITRATE,
  MRL_PROPERTY_VIDEO_WIDTH,
  MRL_PROPERTY_VIDEO_HEIGHT,
  MRL_PROPERTY_VIDEO_ASPECT,
  MRL_PROPERTY_VIDEO_CHANNELS,
  MRL_PROPERTY_VIDEO_STREAMS,
  MRL_PROPERTY_VIDEO_FRAMEDURATION,
} mrl_properties_type_t;

mrl_t *mrl_new (player_t *player, mrl_resource_t res, void *args);
void mrl_add_subtitle (mrl_t *mrl, char *subtitle);
void mrl_free (mrl_t *mrl, int recursive);
void mrl_list_free (mrl_t *mrl);
mrl_type_t mrl_get_type (player_t *player, mrl_t *mrl);
mrl_resource_t mrl_get_resource (player_t *player, mrl_t *mrl);
char *mrl_get_metadata (player_t *player, mrl_t *mrl, mrl_metadata_type_t m);
uint32_t mrl_get_property (player_t *player,
                           mrl_t *mrl, mrl_properties_type_t p);
char *mrl_get_audio_codec (player_t *player, mrl_t *mrl);
char *mrl_get_video_codec (player_t *player, mrl_t *mrl);
off_t mrl_get_size (player_t *player, mrl_t *mrl);

/***************************************************************************/
/*                                                                         */
/* Player to MRL connection                                                */
/*                                                                         */
/***************************************************************************/

typedef enum player_mrl_add {
  PLAYER_MRL_ADD_NOW,
  PLAYER_MRL_ADD_QUEUE
} player_mrl_add_t;

mrl_t *player_mrl_get_current (player_t *player);
void player_mrl_set (player_t *player, mrl_t *mrl);
void player_mrl_append (player_t *player, mrl_t *mrl, player_mrl_add_t when);
void player_mrl_remove (player_t *player);
void player_mrl_remove_all (player_t *player);
void player_mrl_previous (player_t *player);
void player_mrl_next (player_t *player);

/***************************************************************************/
/*                                                                         */
/* Player tuning & properties                                              */
/*                                                                         */
/***************************************************************************/

int player_get_time_pos (player_t *player);
void player_set_loop (player_t *player, int value);
void player_set_shuffle (player_t *player, int value);
void player_set_framedrop (player_t *player, int value);

/***************************************************************************/
/*                                                                         */
/* Playback related controls                                               */
/*                                                                         */
/***************************************************************************/

typedef enum player_pb_seek {
  PLAYER_PB_SEEK_RELATIVE,
  PLAYER_PB_SEEK_ABSOLUTE,
  PLAYER_PB_SEEK_PERCENT,
} player_pb_seek_t;

void player_playback_start (player_t *player);
void player_playback_stop (player_t *player);
void player_playback_pause (player_t *player);
void player_playback_seek (player_t *player, int value, player_pb_seek_t seek);
void player_playback_seek_chapter (player_t *player, int value, int absolute);
void player_playback_speed (player_t *player, float value);

/***************************************************************************/
/*                                                                         */
/* Audio related controls                                                  */
/*                                                                         */
/***************************************************************************/

typedef enum player_mute {
  PLAYER_MUTE_UNKNOWN,
  PLAYER_MUTE_ON,
  PLAYER_MUTE_OFF
} player_mute_t;

int player_audio_volume_get (player_t *player);
void player_audio_volume_set (player_t *player, int value);
player_mute_t player_audio_mute_get (player_t *player);
void player_audio_mute_set (player_t *player, player_mute_t value);
void player_audio_set_delay (player_t *player, int value, int absolute);
void player_audio_select (player_t *player, int audio_id);
void player_audio_prev (player_t *player);
void player_audio_next (player_t *player);

/***************************************************************************/
/*                                                                         */
/* Video related controls                                                  */
/*                                                                         */
/***************************************************************************/

typedef enum player_video_aspect {
  PLAYER_VIDEO_ASPECT_BRIGHTNESS,
  PLAYER_VIDEO_ASPECT_CONTRAST,
  PLAYER_VIDEO_ASPECT_GAMMA,
  PLAYER_VIDEO_ASPECT_HUE,
  PLAYER_VIDEO_ASPECT_SATURATION,
} player_video_aspect_t;

void player_video_set_fullscreen (player_t *player, int value);
void player_video_set_aspect (player_t *player, player_video_aspect_t aspect,
                              int8_t value, int absolute);
void player_video_set_panscan (player_t *player, int8_t value, int absolute);
void player_video_set_aspect_ratio (player_t *player, float value);

/***************************************************************************/
/*                                                                         */
/* Subtitles related controls                                              */
/*                                                                         */
/***************************************************************************/

typedef enum player_sub_alignment {
  PLAYER_SUB_ALIGNMENT_TOP,
  PLAYER_SUB_ALIGNMENT_CENTER,
  PLAYER_SUB_ALIGNMENT_BOTTOM,
} player_sub_alignment_t;

void player_subtitle_set_delay (player_t *player, float value);
void player_subtitle_set_alignment (player_t *player,
                                    player_sub_alignment_t a);
void player_subtitle_set_position (player_t *player, int value);
void player_subtitle_set_visibility (player_t *player, int value);
void player_subtitle_scale (player_t *player, int value, int absolute);
void player_subtitle_select (player_t *player, int sub_id);
void player_subtitle_prev (player_t *player);
void player_subtitle_next (player_t *player);

/***************************************************************************/
/*                                                                         */
/* DVD specific controls                                                   */
/*                                                                         */
/***************************************************************************/

typedef enum player_dvdnav {
  PLAYER_DVDNAV_UP,
  PLAYER_DVDNAV_DOWN,
  PLAYER_DVDNAV_RIGHT,
  PLAYER_DVDNAV_LEFT,
  PLAYER_DVDNAV_MENU,
  PLAYER_DVDNAV_SELECT
} player_dvdnav_t;

void player_dvd_nav (player_t *player, player_dvdnav_t value);
void player_dvd_angle_select (player_t *player, int angle);
void player_dvd_angle_prev (player_t *player);
void player_dvd_angle_next (player_t *player);
void player_dvd_title_select (player_t *player, int title);
void player_dvd_title_prev (player_t *player);
void player_dvd_title_next (player_t *player);

/***************************************************************************/
/*                                                                         */
/* TV/DVB specific controls                                                */
/*                                                                         */
/***************************************************************************/

void player_tv_channel_select (player_t *player, int channel);
void player_tv_channel_prev (player_t *player);
void player_tv_channel_next (player_t *player);

/***************************************************************************/
/*                                                                         */
/* Radio specific controls                                                 */
/*                                                                         */
/***************************************************************************/

void player_radio_channel_select (player_t *player, int channel);
void player_radio_channel_prev (player_t *player);
void player_radio_channel_next (player_t *player);

#ifdef __cplusplus
#if 0 /* avoid EMACS indent */
{
#endif /* 0 */
}
#endif /* __cplusplus */

#endif /* PLAYER_H */
