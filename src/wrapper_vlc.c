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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <vlc/vlc.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "playlist.h"
#include "fs_utils.h"
#include "parse_utils.h"
#include "wrapper_vlc.h"

#define MODULE_NAME "vlc"

#define WAIT_PERIOD    1000 /* in micro-seconds */
#define WAIT_MAX    5000000 /* in micro-seconds */

/* player specific structure */
typedef struct vlc_s {
  libvlc_instance_t *core;
  libvlc_media_player_t *mp;
  libvlc_exception_t ex;
} vlc_t;

/*****************************************************************************/
/*                            common routines                                */
/*****************************************************************************/

static void
vlc_check_exception (player_t *player)
{
  vlc_t *vlc;

  if (!player)
    return;

  vlc = (vlc_t *) player->priv;
  if (!vlc)
    return;

  if (!libvlc_exception_raised (&vlc->ex))
    return;

  pl_log (player, PLAYER_MSG_WARNING,
          MODULE_NAME, libvlc_exception_get_message (&vlc->ex));
  libvlc_exception_clear (&vlc->ex);
}

static char *
vlc_resource_get_uri_network (const char *protocol,
                              mrl_resource_network_args_t *args)
{
  char *uri, *host_file;
  char at[256] = "";
  size_t size, offset;

  if (!args || !args->url || !protocol)
    return NULL;

  size      = strlen (protocol);
  offset    = (strstr (args->url, protocol) == args->url) ? size : 0;
  host_file = strdup (args->url + offset);

  if (!host_file)
    return NULL;

  if (args->username)
  {
    size += 1 + strlen (args->username);
    if (args->password)
    {
      size += 1 + strlen (args->password);
      snprintf (at, sizeof (at), "%s:%s@", args->username, args->password);
    }
    else
      snprintf (at, sizeof (at), "%s@", args->username);
  }
  size += strlen (host_file);

  size++;
  uri = malloc (size);
  if (uri)
    snprintf (uri, size, "%s%s%s", protocol, at, host_file);

  free (host_file);

  return uri;
}

static char *
vlc_resource_get_uri (mrl_t *mrl)
{
  static const char *const protocols[] = {
    /* Local Streams */
    [MRL_RESOURCE_FILE]     = "file://",

    /* Audio CD */
    [MRL_RESOURCE_CDDA]     = "cdda://",
    [MRL_RESOURCE_CDDB]     = "cddb://",

    /* Video discs */
    [MRL_RESOURCE_DVD]      = "dvd://",
    [MRL_RESOURCE_DVDNAV]   = "dvdnav://",
    [MRL_RESOURCE_VCD]      = "vcd://",

    /* Radio/Television */
    [MRL_RESOURCE_RADIO]    = "radio://",
    [MRL_RESOURCE_TV]       = "tv://",

    /* Network Streams */
    [MRL_RESOURCE_FTP]      = "ftp://",
    [MRL_RESOURCE_HTTP]     = "http://",
    [MRL_RESOURCE_MMS]      = "mms://",
    [MRL_RESOURCE_RTP]      = "rtp://",
    [MRL_RESOURCE_RTSP]     = "rtsp://",
    [MRL_RESOURCE_SMB]      = "smb://",
    [MRL_RESOURCE_UDP]      = "udp://",
    [MRL_RESOURCE_UNSV]     = "unsv://",

    [MRL_RESOURCE_UNKNOWN]  = NULL
  };

  if (!mrl)
    return NULL;

  switch (mrl->resource)
  {
  case MRL_RESOURCE_FILE:
  {
    mrl_resource_local_args_t *args = mrl->priv;

    if (!args || !args->location)
      return NULL;

    return strdup (args->location);
  }

  case MRL_RESOURCE_FTP:  /* ftp://username:password@url   */
  case MRL_RESOURCE_HTTP: /* http://username:password@url  */
  case MRL_RESOURCE_MMS:  /* mms://username:password@url   */
  case MRL_RESOURCE_RTP:  /* rtp://username:password@url   */
  case MRL_RESOURCE_RTSP: /* rtsp://username:password@url  */
  case MRL_RESOURCE_SMB:  /* smb://username:password@url   */
  case MRL_RESOURCE_UDP:  /* udp://username:password@url   */
  case MRL_RESOURCE_UNSV: /* unsv://username:password@url  */
    return vlc_resource_get_uri_network (protocols[mrl->resource], mrl->priv);

  default:
    break;
  }

  return NULL;
}

/*****************************************************************************/
/*                              vlc -identify                                */
/*****************************************************************************/

static void
vlc_identify_metadata (mrl_t *mrl, libvlc_media_player_t *mp,
                       libvlc_exception_t *ex)
{
  mrl_metadata_t *meta;
  libvlc_media_t *media;

  if (!mp || !ex || !mrl || !mrl->meta)
    return;

  media = libvlc_media_player_get_media (mp, ex);
  if (!media)
    return;

  meta = mrl->meta;
  if (!meta)
    return;

  meta->title   = libvlc_media_get_meta (media, libvlc_meta_Title,       ex);
  meta->artist  = libvlc_media_get_meta (media, libvlc_meta_Artist,      ex);
  meta->genre   = libvlc_media_get_meta (media, libvlc_meta_Genre,       ex);
  meta->album   = libvlc_media_get_meta (media, libvlc_meta_Album,       ex);
  meta->year    = libvlc_media_get_meta (media, libvlc_meta_Date,        ex);
  meta->track   = libvlc_media_get_meta (media, libvlc_meta_TrackNumber, ex);
  meta->comment = libvlc_media_get_meta (media, libvlc_meta_Description, ex);
}

static void
vlc_identify_audio (mrl_t *mrl, libvlc_media_player_t *mp,
                    libvlc_exception_t *ex)
{
  /* VLC API is not yet complete enough to retrieve these info */
}

static void
vlc_identify_video (mrl_t *mrl, libvlc_media_player_t *mp,
                    libvlc_exception_t *ex)
{
  mrl_properties_video_t *video;
  libvlc_track_description_t *tracks, *t;
  float val;

  if (!mrl || !mrl->prop || !mp || !ex)
    return;

  /* check if MRL actually has video stream */
  if (!libvlc_media_player_has_vout (mp, ex))
    return;

  if (!mrl->prop->video)
    mrl->prop->video = mrl_properties_video_new ();

  video = mrl->prop->video;

  video->width   = libvlc_video_get_width (mp, ex);
  video->height  = libvlc_video_get_height (mp, ex);
  video->aspect  = (uint32_t) (pl_atof (libvlc_video_get_aspect_ratio (mp, ex))
                               * PLAYER_VIDEO_ASPECT_RATIO_MULT);

  video->streams = 0;
  tracks = libvlc_video_get_track_description (mp, ex);
  t = tracks;
  while (t)
  {
    video->streams++;
    t = t->p_next;
  }
  libvlc_track_description_release (tracks);

  val = libvlc_media_player_get_fps (mp, ex);
  video->frameduration =
    (uint32_t) (val ? PLAYER_VIDEO_FRAMEDURATION_RATIO_DIV / val : 0);
}

static void
vlc_identify_properties (mrl_t *mrl, libvlc_media_player_t *mp,
                         libvlc_exception_t *ex)
{
  if (!mrl || !mrl->prop || !mp || !ex)
    return;

  mrl->prop->seekable = libvlc_media_player_is_seekable (mp, ex);

  mrl->prop->length = (uint32_t) libvlc_media_player_get_length (mp, ex);
}

static void
vlc_identify (player_t *player, mrl_t *mrl, int flags)
{
  libvlc_media_player_t *mp;
  libvlc_media_t *media;
  char *uri = NULL;
  vlc_t *vlc;
  const char *options[32] = { "--vout", "dummy", "--aout", "dummy" };
  libvlc_state_t st = libvlc_NothingSpecial;
  int wait = 0;

  if (!player || !mrl)
    return;

  vlc = (vlc_t *) player->priv;
  if (!vlc || !vlc->core || !vlc->mp)
    return;

  uri = vlc_resource_get_uri (mrl);
  if (!uri)
    return;

  mp = libvlc_media_player_new (vlc->core, &vlc->ex);
  if (!mp)
    goto err_mp;

  media = libvlc_media_new (vlc->core, uri, &vlc->ex);
  if (!media)
    goto err_media;

  libvlc_media_add_option (media, *options, &vlc->ex);

  libvlc_media_player_set_media (mp, media, &vlc->ex);
  libvlc_media_player_play (mp, &vlc->ex);

  while (st <= libvlc_Buffering)
  {
    st = libvlc_media_player_get_state (mp, &vlc->ex);
    usleep (WAIT_PERIOD);
    wait += WAIT_PERIOD;
    if (wait >= WAIT_MAX)
      break;
  }

  if (flags & IDENTIFY_VIDEO)
    vlc_identify_video (mrl, mp, &vlc->ex);

  if (flags & IDENTIFY_AUDIO)
    vlc_identify_audio (mrl, mp, &vlc->ex);

  if (flags & IDENTIFY_METADATA)
    vlc_identify_metadata (mrl, mp, &vlc->ex);

  if (flags & IDENTIFY_PROPERTIES)
    vlc_identify_properties (mrl, mp, &vlc->ex);

  libvlc_media_player_stop (mp, &vlc->ex);
  libvlc_media_release (media);

 err_media:
  libvlc_media_player_release (mp);
 err_mp:
  free (uri);
}

/*****************************************************************************/
/*                         vlc private functions                             */
/*****************************************************************************/

static init_status_t
vlc_init (player_t *player)
{
  vlc_t *vlc = NULL;
  const char *vlc_argv[32] = { "vlc" };
  int vlc_argc = 1;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  /* only X11 video out is supported right now */
  if (player->vo != PLAYER_VO_NULL &&
      player->vo != PLAYER_VO_X11 &&
      player->vo != PLAYER_VO_XV &&
      player->vo != PLAYER_VO_GL)
    return PLAYER_INIT_ERROR;

  //vlc_argv[vlc_argc++] = "-vv";
  vlc_argv[vlc_argc++] = "--no-stats";
  vlc_argv[vlc_argc++] = "--intf";
  vlc_argv[vlc_argc++] = "dummy";
  vlc_argv[vlc_argc++] = "--verbose";
  vlc_argv[vlc_argc++] = "0";
  vlc_argv[vlc_argc++] = "--extraintf";
  vlc_argv[vlc_argc++] = "logger";
  vlc_argv[vlc_argc++] = "--ignore-config";
  vlc_argv[vlc_argc++] = "--reset-plugins-cache";
  vlc_argv[vlc_argc++] = "--no-media-library";
  vlc_argv[vlc_argc++] = "--no-one-instance";
  vlc_argv[vlc_argc++] = "--no-osd";
  vlc_argv[vlc_argc++] = "--no-video-title-show" ;

  /* select the video output */
  switch (player->vo)
  {
  case PLAYER_VO_NULL:
    vlc_argv[vlc_argc++] = "--vout";
    vlc_argv[vlc_argc++] = "dummy";
    break;

#ifdef USE_X11
  case PLAYER_VO_X11:
    vlc_argv[vlc_argc++] = "--vout";
    vlc_argv[vlc_argc++] = "x11,dummy";
    break;

  case PLAYER_VO_XV:
    vlc_argv[vlc_argc++] = "--vout";
    vlc_argv[vlc_argc++] = "xvideo,dummy";
    break;

  case PLAYER_VO_GL:
    vlc_argv[vlc_argc++] = "--vout";
    vlc_argv[vlc_argc++] = "glx,dummy";
    break;
#endif

  default:
    break;
  }

  /* select the audio output */
  switch (player->ao)
  {
  case PLAYER_AO_NULL:
    vlc_argv[vlc_argc++] = "--no-audio";
    break;

  case PLAYER_AO_ALSA:
    vlc_argv[vlc_argc++] = "--aout";
    vlc_argv[vlc_argc++] = "alsa,dummy";
    break;

  case PLAYER_AO_OSS:
    vlc_argv[vlc_argc++] = "--aout";
    vlc_argv[vlc_argc++] = "oss,dummy";
    break;

  default:
    break;
  }

  vlc = (vlc_t *) player->priv;
  libvlc_exception_init (&vlc->ex);
  vlc->core = libvlc_new (vlc_argc, vlc_argv, &vlc->ex);
  vlc_check_exception (player);

  if (!vlc->core)
    return PLAYER_INIT_ERROR;

  vlc->mp = libvlc_media_player_new (vlc->core, &vlc->ex);
  if (!vlc->mp)
    return PLAYER_INIT_ERROR;

  return PLAYER_INIT_OK;
}

static void
vlc_uninit (player_t *player)
{
  vlc_t *vlc = NULL;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "uninit");

  if (!player)
    return;

  vlc = (vlc_t *) player->priv;

  if (!vlc)
    return;

  libvlc_exception_clear (&vlc->ex);
  if (vlc->core)
    libvlc_release (vlc->core);
  if (vlc->mp)
    libvlc_media_player_release (vlc->mp);

  free (vlc);
}

static void
vlc_set_verbosity (player_t *player, player_verbosity_level_t level)
{
  vlc_t *vlc;
  int verbosity = -1;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "set_verbosity");

  if (!player)
    return;

  vlc = (vlc_t *) player->priv;
  if (!vlc)
    return;

  switch (level)
  {
  case PLAYER_MSG_INFO:
  case PLAYER_MSG_WARNING:
  case PLAYER_MSG_ERROR:
  case PLAYER_MSG_CRITICAL:
    verbosity = 1;
    break;

  case PLAYER_MSG_NONE:
    verbosity = 0;
    break;

  default:
    break;
  }

  if (vlc->core && verbosity != -1)
    libvlc_set_log_verbosity (vlc->core, verbosity, &vlc->ex);
}

static void
vlc_mrl_retrieve_properties (player_t *player, mrl_t *mrl)
{
  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "mrl_retrieve_properties");

  if (!player || !mrl || !mrl->prop)
    return;

  /* now fetch properties */
  if (mrl->resource == MRL_RESOURCE_FILE)
  {
    mrl_resource_local_args_t *args = mrl->priv;
    if (args && args->location)
    {
      const char *location = args->location;

      if (strstr (location, "file:") == location)
        location += 5;

      mrl->prop->size = pl_file_size (location);
    }
  }

  vlc_identify (player, mrl,
                IDENTIFY_AUDIO | IDENTIFY_VIDEO | IDENTIFY_PROPERTIES);
}

static void
vlc_mrl_retrieve_metadata (player_t *player, mrl_t *mrl)
{
  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "mrl_retrieve_metadata");

  if (!player || !mrl || !mrl->meta)
    return;

  vlc_identify (player, mrl, IDENTIFY_METADATA);
}

static int
vlc_get_time_pos (player_t *player)
{
  float time_pos = 0.0;
  vlc_t *vlc;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "get_time_pos");

  if (!player)
    return -1;

  vlc = (vlc_t *) player->priv;
  time_pos = libvlc_media_player_get_position (vlc->mp, &vlc->ex);

  if (time_pos < 0.0)
    return -1;

  return (int) (time_pos * 1000.0);
}

static playback_status_t
vlc_playback_start (player_t *player)
{
  vlc_t *vlc;
  mrl_t *mrl;
  char *uri = NULL;
  libvlc_media_t *media = NULL;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_start");

  if (!player)
    return PLAYER_PB_FATAL;

  vlc = (vlc_t *) player->priv;

  if (!vlc->core)
    return PLAYER_PB_ERROR;

  mrl = pl_playlist_get_mrl (player->playlist);
  if (!mrl)
    return PLAYER_PB_ERROR;

  uri = vlc_resource_get_uri (mrl);
  if (!uri)
    return PLAYER_PB_ERROR;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "uri: %s", uri);
  media = libvlc_media_new (vlc->core, uri, &vlc->ex);
  free (uri);

  if (!media)
    return PLAYER_PB_ERROR;

  libvlc_media_player_set_media (vlc->mp, media, &vlc->ex);
  libvlc_media_player_play (vlc->mp, &vlc->ex);

  return PLAYER_PB_OK;
}

static void
vlc_playback_stop (player_t *player)
{
  vlc_t *vlc;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_stop");

  if (!player)
    return;

  vlc = (vlc_t *) player->priv;

  libvlc_media_player_stop (vlc->mp, &vlc->ex);
}

static playback_status_t
vlc_playback_pause (player_t *player)
{
  vlc_t *vlc;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_pause");

  if (!player)
    return PLAYER_PB_FATAL;

  vlc = (vlc_t *) player->priv;

  libvlc_media_player_pause (vlc->mp, &vlc->ex);

  return PLAYER_PB_OK;
}

static int
vlc_audio_get_volume (player_t *player)
{
  vlc_t *vlc;
  int volume = -1;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "audio_get_volume");

  if (!player)
    return volume;

  vlc = (vlc_t *) player->priv;
  volume = libvlc_audio_get_volume (vlc->core, &vlc->ex);

  if (volume < 0)
    return -1;

  return volume;
}

static void
vlc_audio_set_volume (player_t *player, int value)
{
  vlc_t *vlc;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "audio_set_volume: %d", value);

  if (!player)
    return;

  vlc = (vlc_t *) player->priv;
  libvlc_audio_set_volume (vlc->core, value, &vlc->ex);
}

static player_mute_t
vlc_audio_get_mute (player_t *player)
{
  player_mute_t mute = PLAYER_MUTE_UNKNOWN;
  vlc_t *vlc;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "audio_get_mute");

  if (!player)
    return mute;

  vlc = (vlc_t *) player->priv;
  mute = libvlc_audio_get_mute (vlc->core, &vlc->ex) ?
    PLAYER_MUTE_ON : PLAYER_MUTE_OFF;

  return mute;
}

static void
vlc_audio_set_mute (player_t *player, player_mute_t value)
{
  vlc_t *vlc;
  int mute = 0;

  if (value == PLAYER_MUTE_UNKNOWN)
    return;

  if (value == PLAYER_MUTE_ON)
    mute = 1;

  pl_log (player, PLAYER_MSG_INFO,
          MODULE_NAME, "audio_set_mute: %s", mute ? "on" : "off");

  if (!player)
    return;

  vlc = (vlc_t *) player->priv;
  libvlc_audio_set_mute (vlc->core, mute , &vlc->ex);
}

/*****************************************************************************/
/*                            Public Wrapper API                             */
/*****************************************************************************/

int
pl_supported_resources_vlc (mrl_resource_t res)
{
  switch (res)
  {
  case MRL_RESOURCE_FILE:
  case MRL_RESOURCE_FTP:
  case MRL_RESOURCE_HTTP:
  case MRL_RESOURCE_MMS:
  case MRL_RESOURCE_RTP:
  case MRL_RESOURCE_RTSP:
  case MRL_RESOURCE_SMB:
  case MRL_RESOURCE_UDP:
  case MRL_RESOURCE_UNSV:
    return 1;

  default:
    return 0;
  }
}

player_funcs_t *
pl_register_functions_vlc (void)
{
  player_funcs_t *funcs = NULL;

  funcs = calloc (1, sizeof (player_funcs_t));
  if (!funcs)
    return NULL;

  funcs->init               = vlc_init;
  funcs->uninit             = vlc_uninit;
  funcs->set_verbosity      = vlc_set_verbosity;

  funcs->mrl_retrieve_props = vlc_mrl_retrieve_properties;
  funcs->mrl_retrieve_meta  = vlc_mrl_retrieve_metadata;
  funcs->mrl_video_snapshot = NULL;

  funcs->get_time_pos       = vlc_get_time_pos;
  funcs->get_percent_pos    = NULL;
  funcs->set_framedrop      = NULL;
  funcs->set_mouse_pos      = NULL;
  funcs->osd_show_text      = NULL;

  funcs->pb_start           = vlc_playback_start;
  funcs->pb_stop            = vlc_playback_stop;
  funcs->pb_pause           = vlc_playback_pause;
  funcs->pb_seek            = NULL;
  funcs->pb_seek_chapter    = NULL;
  funcs->pb_set_speed       = NULL;

  funcs->audio_get_volume   = vlc_audio_get_volume;
  funcs->audio_set_volume   = vlc_audio_set_volume;
  funcs->audio_get_mute     = vlc_audio_get_mute;
  funcs->audio_set_mute     = vlc_audio_set_mute;
  funcs->audio_set_delay    = NULL;
  funcs->audio_select       = NULL;
  funcs->audio_prev         = NULL;
  funcs->audio_next         = NULL;

  funcs->video_set_fs       = NULL;
  funcs->video_set_aspect   = NULL;
  funcs->video_set_panscan  = NULL;
  funcs->video_set_ar       = NULL;

  funcs->sub_set_delay      = NULL;
  funcs->sub_set_alignment  = NULL;
  funcs->sub_set_pos        = NULL;
  funcs->sub_set_visibility = NULL;
  funcs->sub_scale          = NULL;
  funcs->sub_select         = NULL;
  funcs->sub_prev           = NULL;
  funcs->sub_next           = NULL;

  funcs->dvd_nav            = NULL;
  funcs->dvd_angle_set      = NULL;
  funcs->dvd_angle_prev     = NULL;
  funcs->dvd_angle_next     = NULL;
  funcs->dvd_title_set      = NULL;
  funcs->dvd_title_prev     = NULL;
  funcs->dvd_title_next     = NULL;

  funcs->tv_channel_set     = NULL;
  funcs->tv_channel_prev    = NULL;
  funcs->tv_channel_next    = NULL;

  funcs->radio_channel_set  = NULL;
  funcs->radio_channel_prev = NULL;
  funcs->radio_channel_next = NULL;

  funcs->vdr                = NULL;

  return funcs;
}

void *
pl_register_private_vlc (void)
{
  vlc_t *vlc = NULL;

  vlc = calloc (1, sizeof (vlc_t));
  if (!vlc)
    return NULL;

  return vlc;
}
