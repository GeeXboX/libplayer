/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006-2009 Benjamin Zores <ben@geexbox.org>
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
#include "event.h"
#include "fs_utils.h"
#include "parse_utils.h"
#include "window.h"
#include "wrapper_vlc.h"

#define MODULE_NAME "vlc"

#define WAIT_PERIOD    1000 /* in micro-seconds */
#define WAIT_MAX    5000000 /* in micro-seconds */

/* player specific structure */
typedef struct vlc_s {
  libvlc_instance_t *core;
  libvlc_media_player_t *mp;
} vlc_t;

static const libvlc_event_type_t mp_events[] = {
  libvlc_MediaPlayerPlaying,
  libvlc_MediaPlayerPaused,
  libvlc_MediaPlayerEndReached,
  libvlc_MediaPlayerStopped,
};

/*****************************************************************************/
/*                            common routines                                */
/*****************************************************************************/

static void
vlc_event_callback (const libvlc_event_t *ev, void *data)
{
  player_t *player = NULL;
  libvlc_event_type_t type;

  player = (player_t *) data;
  if (!ev || !player)
    return;

  type = ev->type;
  switch (type)
  {
  case libvlc_MediaPlayerEndReached:
    pl_log (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Playback of stream has ended");
    player_event_send (player, PLAYER_EVENT_PLAYBACK_FINISHED);

    pl_window_unmap (player->window);
    break;

  case libvlc_MediaPlayerPlaying:
  case libvlc_MediaPlayerPaused:
  case libvlc_MediaPlayerStopped:
    break;

  default:
    pl_log (player, PLAYER_MSG_INFO, MODULE_NAME,
            "Unknown event received: %s", libvlc_event_type_name (type));
    break;
  }
}

static char *
vlc_resource_get_uri_dvd (const char *protocol,
                          mrl_resource_videodisc_args_t *args)
{
  char *uri;
  char title_start[8] = "";
  char chapter_start[8] = "";
  char angle[8] = "";
  size_t size;

  if (!args || !protocol)
    return NULL;

  size = strlen (protocol);

  if (args->device)
    size += strlen (args->device);

  if (args->title_start)
  {
    /* dvd://@title */
    snprintf (title_start, sizeof (title_start), "@%u", args->title_start);
    size += strlen (title_start);
  }
  if (args->chapter_start)
  {
    /* dvd://@:chapter */
    snprintf (chapter_start, sizeof (chapter_start), "%s:%u",
              args->title_start ? "" : "@",
              args->chapter_start);
    size += strlen (chapter_start);
  }
  if (args->angle)
  {
    /* dvd://@::angle */
    snprintf (angle, sizeof (angle), "%s%s:%u",
              args->title_start   ? "" : "@",
              args->chapter_start ? "" : ":",
              args->angle);
    size += strlen (angle);
  }

  size++;
  uri = malloc (size);
  if (uri)
    snprintf (uri, size, "%s%s%s%s%s",
              protocol, args->device ? args->device : "",
              title_start, chapter_start, angle);

  return uri;
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

  PFREE (host_file);

  return uri;
}

static char *
vlc_resource_get_uri (mrl_t *mrl)
{
  static const char *const protocols[] = {
    /* Local Streams */
    [MRL_RESOURCE_FILE]     = "file://",

    /* Video discs */
    [MRL_RESOURCE_DVD]      = "dvdsimple://",
    [MRL_RESOURCE_DVDNAV]   = "dvd://",

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

  case MRL_RESOURCE_DVD:    /* dvdsimple://device@title:chapter:angle */
  case MRL_RESOURCE_DVDNAV: /* dvd://device@title:chapter:angle       */
    return vlc_resource_get_uri_dvd (protocols[mrl->resource], mrl->priv);

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
vlc_identify_metadata (mrl_t *mrl, libvlc_media_player_t *mp)
{
  mrl_metadata_t *meta;
  libvlc_media_t *media;

  if (!mp || !mrl || !mrl->meta)
    return;

  media = libvlc_media_player_get_media (mp);
  if (!media)
    return;

  meta = mrl->meta;
  if (!meta)
    return;

  meta->title   = libvlc_media_get_meta (media, libvlc_meta_Title);
  meta->artist  = libvlc_media_get_meta (media, libvlc_meta_Artist);
  meta->genre   = libvlc_media_get_meta (media, libvlc_meta_Genre);
  meta->album   = libvlc_media_get_meta (media, libvlc_meta_Album);
  meta->year    = libvlc_media_get_meta (media, libvlc_meta_Date);
  meta->track   = libvlc_media_get_meta (media, libvlc_meta_TrackNumber);
  meta->comment = libvlc_media_get_meta (media, libvlc_meta_Description);
}

static void
vlc_identify_audio (mrl_t *mrl,
                    libvlc_media_player_t *mp, libvlc_media_track_info_t *es)
{
  mrl_properties_audio_t *audio;

  if (!mrl || !mrl->prop || !mp || !es)
    return;

  if (!mrl->prop->audio)
    mrl->prop->audio = mrl_properties_audio_new ();

  audio = mrl->prop->audio;

  audio->bitrate  = es->u.audio.i_rate;
  audio->channels = es->u.audio.i_channels;
}

static void
vlc_identify_video (mrl_t *mrl,
                    libvlc_media_player_t *mp, libvlc_media_track_info_t *es)
{
  mrl_properties_video_t *video;
  libvlc_track_description_t *tracks, *t;
  const char *ar;
  float val;

  if (!mrl || !mrl->prop || !mp)
    return;

  if (!mrl->prop->video)
  {
    int vid = 0;

    /*
     * HACK:
     * VLC is not always able to found the streams with the following
     * resources. But we can consider that they use always a video stream.
     * Here we are sure that the video window is mapped in any cases.
     */
    switch (mrl->resource)
    {
    case MRL_RESOURCE_DVD:
    case MRL_RESOURCE_DVDNAV:
      vid = 1;
      break;

    default:
      break;
    }

    if (vid || es)
      mrl->prop->video = mrl_properties_video_new ();
  }

  if (!es)
    return;

  video = mrl->prop->video;

  video->width   = es->u.video.i_width;
  video->height  = es->u.video.i_height;

  ar = libvlc_video_get_aspect_ratio (mp);
  if (ar)
    video->aspect = (uint32_t) (pl_atof (ar) * PLAYER_VIDEO_ASPECT_RATIO_MULT);

  video->streams = 0;
  tracks = libvlc_video_get_track_description (mp);
  t = tracks;
  while (t)
  {
    video->streams++;
    t = t->p_next;
  }
  libvlc_track_description_release (tracks);

  val = libvlc_media_player_get_fps (mp);
  video->frameduration =
    (uint32_t) (val ? PLAYER_VIDEO_FRAMEDURATION_RATIO_DIV / val : 0);
}

static void
vlc_identify_properties (mrl_t *mrl, libvlc_media_player_t *mp)
{
  if (!mrl || !mrl->prop || !mp)
    return;

  mrl->prop->seekable = libvlc_media_player_is_seekable (mp);

  mrl->prop->length = (uint32_t) libvlc_media_player_get_length (mp);
}

static void
vlc_identify (player_t *player, mrl_t *mrl, int flags)
{
  libvlc_media_player_t *mp;
  libvlc_media_t *media;
  char *uri = NULL;
  vlc_t *vlc;
  const char *options[] = {
    ":vout=dummy",
    ":aout=dummy",
    ":sout=#description",
  };
  libvlc_media_track_info_t *esv = NULL, *esa = NULL;
  libvlc_media_track_info_t *es = NULL;
  libvlc_state_t st = libvlc_NothingSpecial;
  int wait = 0;
  unsigned int i;
  unsigned int es_count;

  if (!player || !mrl)
    return;

  vlc = player->priv;
  if (!vlc || !vlc->core || !vlc->mp)
    return;

  uri = vlc_resource_get_uri (mrl);
  if (!uri)
    return;

  mp = libvlc_media_player_new (vlc->core);
  if (!mp)
    goto err_mp;

  media = libvlc_media_new_location (vlc->core, uri);
  if (!media)
    goto err_media;

  for (i = 0; i < ARRAY_NB_ELEMENTS (options); i++)
    libvlc_media_add_option (media, options[i]);

  libvlc_media_player_set_media (mp, media);
  libvlc_media_player_play (mp);

  /*
   * FIXME
   * Most of time, vlc_identify() is unable to retrieve the properties
   * because libvlc must be playing in order to read the informations.
   * The code below waits that the Playing state begins, but it is not
   * sufficient for many streams. It suggests that at least one frame
   * or more must be decoded.
   */
  while (st <= libvlc_Buffering)
  {
    st = libvlc_media_player_get_state (mp);
    usleep (WAIT_PERIOD);
    wait += WAIT_PERIOD;
    if (wait >= WAIT_MAX)
      break;
  }

  libvlc_media_parse (media);
  es_count = libvlc_media_get_tracks_info (media, &es);
  for (i = 0; i < es_count; i++)
  {
    if (!esv && es[i].i_type == libvlc_track_video)
      esv = es + i;
    else if (!esa && es[i].i_type == libvlc_track_audio)
      esa = es + i;
  }

  if (flags & IDENTIFY_VIDEO)
    vlc_identify_video (mrl, mp, esv);

  if (flags & IDENTIFY_AUDIO)
    vlc_identify_audio (mrl, mp, esa);

  if (flags & IDENTIFY_METADATA)
    vlc_identify_metadata (mrl, mp);

  if (flags & IDENTIFY_PROPERTIES)
    vlc_identify_properties (mrl, mp);

  libvlc_media_player_stop (mp);
  libvlc_media_release (media);

  PFREE (es);

 err_media:
  libvlc_media_player_release (mp);
 err_mp:
  PFREE (uri);
}

/*****************************************************************************/
/*                         vlc private functions                             */
/*****************************************************************************/

static init_status_t
vlc_init (player_t *player)
{
  vlc_t *vlc = NULL;
  const char *vlc_argv[32];
  libvlc_event_manager_t *ev;
  int vlc_argc = 0;
  int use_x11 = 0;
  unsigned int i;
  uint32_t winid = 0;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  vlc_argv[vlc_argc++] = "--no-stats";
  vlc_argv[vlc_argc++] = "--verbose";
  vlc_argv[vlc_argc++] = "0";
  vlc_argv[vlc_argc++] = "--no-media-library";
  vlc_argv[vlc_argc++] = "--no-osd";
  vlc_argv[vlc_argc++] = "--no-video-title-show" ;

  if (player->x11_display)
  {
    vlc_argv[vlc_argc++] = "--x11-display";
    vlc_argv[vlc_argc++] = player->x11_display;
  }

  /* select the video output */
  switch (player->vo)
  {
  case PLAYER_VO_NULL:
    vlc_argv[vlc_argc++] = "--no-video";
    break;

#ifdef HAVE_WIN_XCB
  case PLAYER_VO_X11:
    vlc_argv[vlc_argc++] = "--vout";
    vlc_argv[vlc_argc++] = "x11,dummy";
    use_x11 = 1;
    break;

  case PLAYER_VO_XV:
    vlc_argv[vlc_argc++] = "--vout";
    vlc_argv[vlc_argc++] = "xvideo,dummy";
    use_x11 = 1;
    break;

  case PLAYER_VO_GL:
    vlc_argv[vlc_argc++] = "--vout";
    vlc_argv[vlc_argc++] = "glx,dummy";
    use_x11 = 1;
    break;
#endif /* HAVE_WIN_XCB */

  case PLAYER_VO_AUTO:
    use_x11 = 1;
    break;

  default:
    return PLAYER_INIT_ERROR;
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

  case PLAYER_AO_PULSE:
    vlc_argv[vlc_argc++] = "--aout";
    vlc_argv[vlc_argc++] = "pulse,dummy";
    break;

  case PLAYER_AO_AUTO:
  default:
    break;
  }

  if (use_x11)
  {
#ifdef HAVE_WIN_XCB
    int rc;

    rc = pl_window_init (player->window);
    if (!rc && player->vo != PLAYER_VO_AUTO)
    {
      pl_log (player, PLAYER_MSG_ERROR,
              MODULE_NAME, "initialization for X has failed");
      return PLAYER_INIT_ERROR;
    }

    winid = pl_window_winid_get (player->window);
#else /* HAVE_WIN_XCB */
    pl_log (player, PLAYER_MSG_ERROR, MODULE_NAME,
            "auto-detection for window is not enabled without X11 support");
    return PLAYER_INIT_ERROR;
#endif /* !HAVE_WIN_XCB */
  }

  vlc = player->priv;
  vlc->core = libvlc_new (vlc_argc, vlc_argv);

  if (!vlc->core)
    return PLAYER_INIT_ERROR;

  vlc->mp = libvlc_media_player_new (vlc->core);
  if (!vlc->mp)
    return PLAYER_INIT_ERROR;

  if (winid)
    libvlc_media_player_set_xwindow (vlc->mp, winid);

  libvlc_video_set_key_input   (vlc->mp, 0);
  libvlc_video_set_mouse_input (vlc->mp, 0);

  /* register the event manager */
  ev = libvlc_media_player_event_manager (vlc->mp);
  if (!ev)
    return PLAYER_INIT_ERROR;

  for (i = 0; i < ARRAY_NB_ELEMENTS (mp_events); i++)
    libvlc_event_attach (ev, mp_events[i],
                         vlc_event_callback, player);

  return PLAYER_INIT_OK;
}

static void
vlc_uninit (player_t *player)
{
  vlc_t *vlc = NULL;
  libvlc_media_t *media;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "uninit");

  if (!player)
    return;

  vlc = player->priv;

  if (!vlc)
    return;

  if (vlc->mp)
  {
    media = libvlc_media_player_get_media (vlc->mp);
    if (media)
      libvlc_media_release (media);
    libvlc_media_player_release (vlc->mp);
  }
  if (vlc->core)
    libvlc_release (vlc->core);

  pl_window_uninit (player->window);

  PFREE (vlc);
}

static void
vlc_set_verbosity (player_t *player, player_verbosity_level_t level)
{
  vlc_t *vlc;
  int verbosity = -1;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "set_verbosity");

  if (!player)
    return;

  vlc = player->priv;
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
    libvlc_set_log_verbosity (vlc->core, verbosity);
}

static void
vlc_mrl_retrieve_properties (player_t *player, mrl_t *mrl)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "mrl_retrieve_properties");

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
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "mrl_retrieve_metadata");

  if (!player || !mrl || !mrl->meta)
    return;

  vlc_identify (player, mrl, IDENTIFY_METADATA);
}

static void
vlc_mrl_video_snapshot (player_t *player, mrl_t *mrl, pl_unused int pos,
                        pl_unused mrl_snapshot_t t, const char *dst)
{
  vlc_t *vlc;
  unsigned int width, height;
  libvlc_media_track_info_t *es = NULL;
  libvlc_media_track_info_t *esv = NULL;
  libvlc_media_t *media;
  unsigned int es_count;
  unsigned int i;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "mrl_video_snapshot");

  if (!player || !mrl || !mrl->meta)
    return;

  vlc = player->priv;
  if (!vlc || !vlc->mp)
    return;

  media = libvlc_media_player_get_media (vlc->mp);
  if (!media)
    return;

  /*
   * FIXME
   * Like vlc_identify and according to the documentation of libvlc, the media
   * must be played at least one time, else ES array will be empty.
   */
  es_count = libvlc_media_get_tracks_info (media, &es);
  for (i = 0; i < es_count; i++)
    if (es[i].i_type == libvlc_track_video)
    {
      esv = es + i;
      break;
    }

  if (!esv)
    goto out;

  width  = esv->u.video.i_width;
  height = esv->u.video.i_height;

  libvlc_video_take_snapshot (vlc->mp, 0, dst, width, height);

 out:
  PFREE (es);
}

static int
vlc_get_time_pos (player_t *player)
{
  float time_pos = 0.0;
  vlc_t *vlc;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "get_time_pos");

  if (!player)
    return -1;

  vlc = player->priv;
  if (!vlc || !vlc->mp)
    return -1;

  time_pos = libvlc_media_player_get_time (vlc->mp);

  return (time_pos < 0.0) ? -1: (int) time_pos;
}

static int
vlc_get_percent_pos (player_t *player)
{
  float pos = 0.0;
  vlc_t *vlc;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "get_percent_pos");

  if (!player)
    return -1;

  vlc = player->priv;
  if (!vlc || !vlc->mp)
    return -1;

  pos = libvlc_media_player_get_position (vlc->mp);
  return (pos < 0.0) ? -1 : (int) (pos * 100.0);
}

static playback_status_t
vlc_playback_start (player_t *player)
{
  vlc_t *vlc;
  mrl_t *mrl;
  char *uri = NULL;
  libvlc_media_t *media = NULL;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_start");

  if (!player)
    return PLAYER_PB_FATAL;

  vlc = player->priv;
  if (!vlc || !vlc->mp)
    return PLAYER_PB_ERROR;

  if (!vlc->core)
    return PLAYER_PB_ERROR;

  mrl = pl_playlist_get_mrl (player->playlist);
  if (!mrl)
    return PLAYER_PB_ERROR;

  uri = vlc_resource_get_uri (mrl);
  if (!uri)
    return PLAYER_PB_ERROR;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "uri: %s", uri);
  media = libvlc_media_new_location (vlc->core, uri);
  PFREE (uri);

  if (!media)
    return PLAYER_PB_ERROR;

  if (MRL_USES_VO (mrl))
    pl_window_map (player->window);

  libvlc_media_player_set_media (vlc->mp, media);
  libvlc_media_player_play (vlc->mp);

  return PLAYER_PB_OK;
}

static void
vlc_playback_stop (player_t *player)
{
  mrl_t *mrl;
  vlc_t *vlc;
  libvlc_media_t *media;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_stop");

  if (!player)
    return;

  vlc = player->priv;
  if (!vlc || !vlc->mp)
    return;

  mrl = pl_playlist_get_mrl (player->playlist);
  if (MRL_USES_VO (mrl))
    pl_window_unmap (player->window);

  media = libvlc_media_player_get_media (vlc->mp);
  libvlc_media_player_stop (vlc->mp);
  libvlc_media_release (media);
}

static playback_status_t
vlc_playback_pause (player_t *player)
{
  vlc_t *vlc;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_pause");

  if (!player)
    return PLAYER_PB_FATAL;

  vlc = player->priv;
  if (!vlc || !vlc->mp)
    return PLAYER_PB_FATAL;

  if (libvlc_media_player_is_playing (vlc->mp) &&
      libvlc_media_player_can_pause (vlc->mp))
    libvlc_media_player_pause (vlc->mp);
  else
    libvlc_media_player_play (vlc->mp);

  return PLAYER_PB_OK;
}

static void
vlc_playback_seek (player_t *player, int value, player_pb_seek_t seek)
{
  vlc_t *vlc;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "playback_seek: %d %d", value, seek);

  if (!player)
    return;

  vlc = player->priv;
  if (!vlc || !vlc->mp)
    return;

  switch (seek)
  {
  default:
  case PLAYER_PB_SEEK_RELATIVE:
  {
    libvlc_time_t pos_time, length;

    length = libvlc_media_player_get_length (vlc->mp);
    pos_time = libvlc_media_player_get_time (vlc->mp);
    pos_time += value;

    if (pos_time < 0)
      pos_time = 0;
    if (pos_time > length)
      break;

    libvlc_media_player_set_time (vlc->mp, pos_time);
    break;
  }
  case PLAYER_PB_SEEK_PERCENT:
    libvlc_media_player_set_position (vlc->mp, value / 100.0);
    break;
  case PLAYER_PB_SEEK_ABSOLUTE:
    libvlc_media_player_set_time (vlc->mp, value);
    break;
  }
}

static void
vlc_playback_seek_chapter (player_t *player, int value, int absolute)
{
  vlc_t *vlc;
  int chapter;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "playback_seek_chapter: %i %i", value, absolute);

  if (!player)
    return;

  vlc = player->priv;
  if (!vlc || !vlc->mp)
    return;

  chapter = absolute ? value :
    libvlc_media_player_get_chapter (vlc->mp) + value;

  if (chapter > libvlc_media_player_get_chapter_count (vlc->mp))
    return;

  libvlc_media_player_set_chapter (vlc->mp, chapter);
}

static void
vlc_playback_set_speed (player_t *player, float value)
{
  vlc_t *vlc;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "playback_set_speed %.2f", value);

  if (!player)
    return;

  vlc = player->priv;
  if (!vlc || !vlc->mp)
    return;

  libvlc_media_player_set_rate (vlc->mp, value);
}

static int
vlc_audio_get_volume (player_t *player)
{
  vlc_t *vlc;
  int volume = -1;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "audio_get_volume");

  if (!player)
    return volume;

  vlc = player->priv;
  volume = libvlc_audio_get_volume (vlc->mp);

  return (volume < 0) ? -1 : volume;
}

static void
vlc_audio_set_volume (player_t *player, int value)
{
  vlc_t *vlc;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "audio_set_volume: %d", value);

  if (!player)
    return;

  vlc = player->priv;
  libvlc_audio_set_volume (vlc->mp, value);
}

static player_mute_t
vlc_audio_get_mute (player_t *player)
{
  player_mute_t mute = PLAYER_MUTE_UNKNOWN;
  vlc_t *vlc;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "audio_get_mute");

  if (!player)
    return mute;

  vlc = player->priv;
  mute = libvlc_audio_get_mute (vlc->mp) ?
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

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "audio_set_mute: %s", mute ? "on" : "off");

  if (!player)
    return;

  vlc = player->priv;
  libvlc_audio_set_mute (vlc->mp, mute);
}

static void
vlc_video_set_ar (player_t *player, float value)
{
  vlc_t *vlc;
  char *ar;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "video_set_ar: %.2f", value);

  if (!player)
    return;

  vlc = player->priv;
  if (!vlc || !vlc->mp)
    return;

  if (value >= (221.0 / 100.0))
    ar = "221:100";
  else if (value >= (16.0 / 9.0))
    ar = "16:9";
  else if (value >= (16.0 / 10.0))
    ar = "16:10";
  else if (value >= (4.0 / 3.0))
    ar = "4:3";
  else if (value >= (5.0 / 4.0))
    ar = "5:4";
  else if (value >= 1.0)
    ar = "1:1";
  else
    return;

  libvlc_video_set_aspect_ratio (vlc->mp, ar);
}

static void
vlc_sub_select (player_t *player, int value)
{
  vlc_t *vlc;
  int max;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "sub_select: %i", value);

  if (!player)
    return;

  vlc = player->priv;
  if (!vlc || !vlc->mp)
    return;

  max = libvlc_video_get_spu_count (vlc->mp);
  if (value < 0)
    value = 0;
  if (value > max)
    value = max;

  libvlc_video_set_spu (vlc->mp, value);
}

static void
vlc_sub_prev (player_t *player)
{
  vlc_t *vlc;
  int cur, max, val;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "sub_prev");

  if (!player)
    return;

  vlc = player->priv;
  if (!vlc || !vlc->mp)
    return;

  cur = libvlc_video_get_spu (vlc->mp);
  max = libvlc_video_get_spu_count (vlc->mp);
  val = cur - 1;

  /* if current is first one, just cycle */
  if (val < 0)
    val = max;

  libvlc_video_set_spu (vlc->mp, val);
}

static void
vlc_sub_next (player_t *player)
{
  vlc_t *vlc;
  int cur, max, val;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "sub_next");

  if (!player)
    return;

  vlc = player->priv;
  if (!vlc || !vlc->mp)
    return;

  cur = libvlc_video_get_spu (vlc->mp);
  max = libvlc_video_get_spu_count (vlc->mp);
  val = cur + 1;

  /* if current is last one, just cycle */
  if (val > max)
    val = 0;

  libvlc_video_set_spu (vlc->mp, val);
}

static void
vlc_dvd_title_set (player_t *player, int value)
{
  vlc_t *vlc;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "dvd_title_set: %i", value);

  if (!player)
    return;

  vlc = player->priv;
  if (!vlc || !vlc->mp)
    return;

  if (value < 1 || value > 99)
    return;

  if (value > libvlc_media_player_get_title_count (vlc->mp))
    return;

  libvlc_media_player_set_title (vlc->mp, value);
}

static void
vlc_dvd_title_prev (player_t *player)
{
  vlc_t *vlc;
  int value;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "dvd_title_prev");

  if (!player)
    return;

  vlc = player->priv;
  if (!vlc || !vlc->mp)
    return;

  value = libvlc_media_player_get_title_count (vlc->mp) - 1;
  vlc_dvd_title_set (player, value);
}

static void
vlc_dvd_title_next (player_t *player)
{
  vlc_t *vlc;
  int value;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "dvd_title_next");

  if (!player)
    return;

  vlc = player->priv;
  if (!vlc || !vlc->mp)
    return;

  value = libvlc_media_player_get_title_count (vlc->mp) + 1;
  vlc_dvd_title_set (player, value);
}

/*****************************************************************************/
/*                            Public Wrapper API                             */
/*****************************************************************************/

int
pl_supported_resources_vlc (mrl_resource_t res)
{
  switch (res)
  {
  case MRL_RESOURCE_DVD:
  case MRL_RESOURCE_DVDNAV:
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

  funcs = PCALLOC (player_funcs_t, 1);
  if (!funcs)
    return NULL;

  funcs->init               = vlc_init;
  funcs->uninit             = vlc_uninit;
  funcs->set_verbosity      = vlc_set_verbosity;

  funcs->mrl_retrieve_props = vlc_mrl_retrieve_properties;
  funcs->mrl_retrieve_meta  = vlc_mrl_retrieve_metadata;
  funcs->mrl_video_snapshot = vlc_mrl_video_snapshot;

  funcs->get_time_pos       = vlc_get_time_pos;
  funcs->get_percent_pos    = vlc_get_percent_pos;
  funcs->set_framedrop      = NULL;
  funcs->set_mouse_pos      = NULL;
  funcs->osd_show_text      = NULL;
  funcs->osd_state          = NULL;

  funcs->pb_start           = vlc_playback_start;
  funcs->pb_stop            = vlc_playback_stop;
  funcs->pb_pause           = vlc_playback_pause;
  funcs->pb_seek            = vlc_playback_seek;
  funcs->pb_seek_chapter    = vlc_playback_seek_chapter;
  funcs->pb_set_speed       = vlc_playback_set_speed;

  funcs->audio_get_volume   = vlc_audio_get_volume;
  funcs->audio_set_volume   = vlc_audio_set_volume;
  funcs->audio_get_mute     = vlc_audio_get_mute;
  funcs->audio_set_mute     = vlc_audio_set_mute;
  funcs->audio_set_delay    = NULL;
  funcs->audio_select       = NULL;
  funcs->audio_prev         = NULL;
  funcs->audio_next         = NULL;

  funcs->video_set_aspect   = NULL;
  funcs->video_set_panscan  = NULL;
  funcs->video_set_ar       = vlc_video_set_ar;

  funcs->sub_set_delay      = NULL;
  funcs->sub_set_alignment  = NULL;
  funcs->sub_set_pos        = NULL;
  funcs->sub_set_visibility = NULL;
  funcs->sub_scale          = NULL;
  funcs->sub_select         = vlc_sub_select;
  funcs->sub_prev           = vlc_sub_prev;
  funcs->sub_next           = vlc_sub_next;

  funcs->dvd_nav            = NULL;
  funcs->dvd_angle_set      = NULL;
  funcs->dvd_angle_prev     = NULL;
  funcs->dvd_angle_next     = NULL;
  funcs->dvd_title_set      = vlc_dvd_title_set;
  funcs->dvd_title_prev     = vlc_dvd_title_prev;
  funcs->dvd_title_next     = vlc_dvd_title_next;

  funcs->tv_channel_set     = NULL;
  funcs->tv_channel_prev    = NULL;
  funcs->tv_channel_next    = NULL;

  funcs->radio_channel_set  = NULL;
  funcs->radio_channel_prev = NULL;
  funcs->radio_channel_next = NULL;

  funcs->vdr                = PL_NOT_SUPPORTED;

  return funcs;
}

void *
pl_register_private_vlc (void)
{
  vlc_t *vlc = NULL;

  vlc = PCALLOC (vlc_t, 1);
  if (!vlc)
    return NULL;

  return vlc;
}
