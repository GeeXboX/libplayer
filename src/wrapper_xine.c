/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006-2008 Benjamin Zores <ben@geexbox.org>
 * Copyright (C) 2007-2009 Mathieu Schroeter <mathieu@schroetersa.ch>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>

#include <xine.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "playlist.h"
#include "event.h"
#include "fs_utils.h"
#include "parse_utils.h"
#include "window.h"
#include "wrapper_xine.h"

#define MODULE_NAME "xine"

/* player specific structure */
typedef struct xine_player_s {
  xine_t *xine;
  xine_stream_t *stream;
  xine_event_queue_t *event_queue;
  xine_video_port_t *vo_port;
  xine_audio_port_t *ao_port;

  int mouse_x, mouse_y; /* mouse coord set by xine_player_set_mouse_pos() */
} xine_player_t;


/* private functions */
static void
xine_player_event_listener_cb (void *user_data, const xine_event_t *event)
{
  player_t *player = NULL;

  player = user_data;
  if (!player)
    return;

  switch (event->type)
  {
  case XINE_EVENT_UI_PLAYBACK_FINISHED:
  {
    pl_log (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Playback of stream has ended");
    player_event_send (player, PLAYER_EVENT_PLAYBACK_FINISHED);

    pl_window_unmap (player->window);
    break;
  }
  case XINE_EVENT_PROGRESS:
  {
    xine_progress_data_t *pevent = (xine_progress_data_t *) event->data;
    pl_log (player, PLAYER_MSG_INFO,
            MODULE_NAME, "%s [%d%%]", pevent->description, pevent->percent);
    break;
  }
  }
}

static void
send_event (player_t *player, int event, void *data, int data_size)
{
  xine_player_t *x = NULL;
  xine_event_t xine_event;

  if (!player)
    return;

  x = player->priv;

  if (!x || !x->stream)
    return;

  xine_event.type = event;
  xine_event.stream = x->stream;
  xine_event.data = data;
  xine_event.data_length = data_size;

  xine_event_send (x->stream, &xine_event);
}

static char *
xi_resource_get_uri_local (const char *protocol,
                           mrl_resource_local_args_t *args)
{
  if (!args || !args->location || !protocol)
    return NULL;

  if (strchr (args->location, ':')
      && strncmp (args->location, protocol, strlen (protocol)))
    return NULL;

  return strdup (args->location);
}

static char *
xi_resource_get_uri_dvd (const char *protocol,
                         mrl_resource_videodisc_args_t *args)
{
  char *uri;
  char title_start[8] = "";
  size_t size;

  if (!args || !protocol)
    return NULL;

  size = strlen (protocol);

  if (args->device)
    size += strlen (args->device);

  if (args->title_start)
  {
    size += 1 + pl_count_nb_dec (args->title_start);
    snprintf (title_start, sizeof (title_start), "/%i", args->title_start);
  }

  size++;
  uri = malloc (size);
  if (uri)
    snprintf (uri, size, "%s%s%s",
              protocol, args->device ? args->device : "", title_start);

  return uri;
}

static char *
xi_resource_get_uri_vdr (const char *protocol, mrl_resource_tv_args_t *args)
{
  char *uri;
  size_t size;

  if (!protocol)
    return NULL;

  size = strlen (protocol);

  if (!args || !args->device)
    return strdup (protocol);

  if (args->driver)
    size += 1 + strlen (args->driver);

  size += strlen (args->device) + 1;
  uri = malloc (size);
  snprintf (uri, size, "%s%s%s%s",
            protocol, args->device,
            args->driver ? "#" : "",
            args->driver ? args->driver : "");

  return uri;
}

static char *
xi_resource_get_uri_network (const char *protocol,
                             mrl_resource_network_args_t *args)
{
  char *uri, *host_file;
  size_t size;

  if (!args || !args->url || !protocol)
    return NULL;

  size = strlen (protocol);

  if (strstr (args->url, protocol) == args->url)
    host_file = strdup (args->url + size);
  else
    host_file = strdup (args->url);

  if (!host_file)
    return NULL;

  size += strlen (host_file);

  size++;
  uri = malloc (size);
  if (uri)
    snprintf (uri, size, "%s%s", protocol, host_file);

  PFREE (host_file);

  return uri;
}

static char *
xine_resource_get_uri (mrl_t *mrl)
{
  static const char *const protocols[] = {
    /* Local Streams */
    [MRL_RESOURCE_FILE]     = "file:",

    /* Video discs */
    [MRL_RESOURCE_DVD]      = "dvd:",
    [MRL_RESOURCE_DVDNAV]   = "dvd:",

    /* Radio/Television */
    [MRL_RESOURCE_VDR]      = "vdr:/",

    /* Network Streams */
    [MRL_RESOURCE_HTTP]     = "http://",
    [MRL_RESOURCE_MMS]      = "mms://",
    [MRL_RESOURCE_NETVDR]   = "netvdr://",
    [MRL_RESOURCE_RTP]      = "rtp://",
    [MRL_RESOURCE_TCP]      = "tcp://",
    [MRL_RESOURCE_UDP]      = "udp://",

    [MRL_RESOURCE_UNKNOWN]  = NULL
  };

  if (!mrl)
    return NULL;

  switch (mrl->resource)
  {
  case MRL_RESOURCE_FILE: /* file:location */
    return xi_resource_get_uri_local (protocols[mrl->resource], mrl->priv);

  case MRL_RESOURCE_DVD:    /* dvd:device/title_start */
  case MRL_RESOURCE_DVDNAV: /* dvd:device/title_start */
    return xi_resource_get_uri_dvd (protocols[mrl->resource], mrl->priv);

  case MRL_RESOURCE_VDR: /* vdr:/device#driver */
    return xi_resource_get_uri_vdr (protocols[mrl->resource], mrl->priv);

  case MRL_RESOURCE_HTTP:   /* http://host...     */
  case MRL_RESOURCE_MMS:    /* mms://host...      */
  case MRL_RESOURCE_NETVDR: /* netvdr://host:port */
  case MRL_RESOURCE_RTP:    /* rtp://host:port    */
  case MRL_RESOURCE_TCP:    /* tcp://host:port    */
  case MRL_RESOURCE_UDP:    /* udp://host:port    */
    return xi_resource_get_uri_network (protocols[mrl->resource], mrl->priv);

  default:
    break;
  }

  return NULL;
}

/*****************************************************************************/
/*                              xine -identify                               */
/*****************************************************************************/

static void
xine_identify_metadata_dvd (mrl_t *mrl, xine_stream_t *stream)
{
  const char *s;
  mrl_metadata_dvd_t *dvd = mrl->meta->priv;

  if (!dvd)
    return;

  s = xine_get_meta_info (stream, XINE_META_INFO_TITLE);
  if (s)
  {
    PFREE (dvd->volumeid);
    dvd->volumeid = strdup (s);
  }

  dvd->titles =
    (uint8_t) xine_get_stream_info (stream, XINE_STREAM_INFO_DVD_TITLE_COUNT);
}

static void
xine_identify_metadata_clip (mrl_t *mrl, xine_stream_t *stream)
{
  const char *s;
  mrl_metadata_t *meta = mrl->meta;

  s = xine_get_meta_info (stream, XINE_META_INFO_TITLE);
  if (s)
  {
    PFREE (meta->title);
    meta->title = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_ARTIST);
  if (s)
  {
    PFREE (meta->artist);
    meta->artist = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_GENRE);
  if (s)
  {
    PFREE (meta->genre);
    meta->genre = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_ALBUM);
  if (s)
  {
    PFREE (meta->album);
    meta->album = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_YEAR);
  if (s)
  {
    PFREE (meta->year);
    meta->year = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_TRACK_NUMBER);
  if (s)
  {
    PFREE (meta->track);
    meta->track = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_COMMENT);
  if (s)
  {
    PFREE (meta->comment);
    meta->comment = strdup (s);
  }
}

static void
xine_identify_metadata (mrl_t *mrl, xine_stream_t *stream)
{
  if (!mrl || !mrl->meta || !stream)
    return;

  switch (mrl->resource)
  {
  case MRL_RESOURCE_DVD:
  case MRL_RESOURCE_DVDNAV:
    xine_identify_metadata_dvd (mrl, stream);
    break;

  default:
    xine_identify_metadata_clip (mrl, stream);
    break;
  }
}

static void
xine_identify_audio (mrl_t *mrl, xine_stream_t *stream)
{
  const char *s;
  mrl_properties_audio_t *audio;

  if (!mrl || !mrl->prop || !stream)
    return;

  if (!xine_get_stream_info (stream, XINE_STREAM_INFO_HAS_AUDIO))
    return;

  if (!mrl->prop->audio)
    mrl->prop->audio = mrl_properties_audio_new ();

  audio = mrl->prop->audio;

  s = xine_get_meta_info (stream, XINE_META_INFO_AUDIOCODEC);
  if (s)
  {
    PFREE (audio->codec);
    audio->codec = strdup (s);
  }

  audio->bitrate =
    xine_get_stream_info (stream, XINE_STREAM_INFO_AUDIO_BITRATE);

  audio->bits =
    xine_get_stream_info (stream, XINE_STREAM_INFO_AUDIO_BITS);

  audio->channels =
    xine_get_stream_info (stream, XINE_STREAM_INFO_AUDIO_CHANNELS);

  audio->samplerate =
    xine_get_stream_info (stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE);
}

static void
xine_identify_video (mrl_t *mrl, xine_stream_t *stream)
{
  const char *s;
  mrl_properties_video_t *video;

  if (!mrl || !mrl->prop || !stream)
    return;

  if (!xine_get_stream_info (stream, XINE_STREAM_INFO_HAS_VIDEO))
    return;

  if (!mrl->prop->video)
    mrl->prop->video = mrl_properties_video_new ();

  video = mrl->prop->video;

  s = xine_get_meta_info (stream, XINE_META_INFO_VIDEOCODEC);
  if (s)
  {
    PFREE (video->codec);
    video->codec = strdup (s);
  }

  video->bitrate =
    xine_get_stream_info (stream, XINE_STREAM_INFO_VIDEO_BITRATE);

  video->width =
    xine_get_stream_info (stream, XINE_STREAM_INFO_VIDEO_WIDTH);

  video->height =
    xine_get_stream_info (stream, XINE_STREAM_INFO_VIDEO_HEIGHT);

  video->aspect =
    xine_get_stream_info (stream, XINE_STREAM_INFO_VIDEO_RATIO);

  video->channels =
    xine_get_stream_info (stream, XINE_STREAM_INFO_VIDEO_CHANNELS);

  video->streams =
    xine_get_stream_info (stream, XINE_STREAM_INFO_VIDEO_STREAMS);

  video->frameduration =
    xine_get_stream_info (stream, XINE_STREAM_INFO_FRAME_DURATION);
}

static void
xine_identify_properties (mrl_t *mrl, xine_stream_t *stream)
{
  int length = 0;
  int ret;

  if (!mrl || !mrl->prop || !stream)
    return;

  mrl->prop->seekable =
    xine_get_stream_info (stream, XINE_STREAM_INFO_SEEKABLE);

  ret = xine_get_pos_length (stream, NULL, NULL, &length);
  if (ret && length > 0)
    mrl->prop->length = length;
}

static void
xine_identify (player_t *player, mrl_t *mrl, int flags)
{
  xine_player_t *x;
  xine_stream_t *stream;
  xine_video_port_t *vo;
  xine_audio_port_t *ao;
  char *uri = NULL;

  if (!player || !mrl)
    return;

  x = player->priv;

  if (!x)
    return;

  uri = xine_resource_get_uri (mrl);
  if (!uri)
    return;

  ao = xine_open_audio_driver (x->xine, "none", NULL);

  if (!ao)
    goto err_ao;

  vo = xine_open_video_driver (x->xine, "none", XINE_VISUAL_TYPE_NONE, NULL);

  if (!vo)
    goto err_vo;

  stream = xine_stream_new (x->xine, ao, vo);

  if (stream)
  {
    xine_open (stream, uri);

    if (flags & IDENTIFY_VIDEO)
      xine_identify_video (mrl, stream);

    if (flags & IDENTIFY_AUDIO)
      xine_identify_audio (mrl, stream);

    if (flags & IDENTIFY_METADATA)
      xine_identify_metadata (mrl, stream);

    if (flags & IDENTIFY_PROPERTIES)
      xine_identify_properties (mrl, stream);

    xine_close (stream);
    xine_dispose (stream);
  }

  xine_close_video_driver (x->xine, vo);
 err_vo:
  xine_close_audio_driver (x->xine, ao);
 err_ao:
  PFREE (uri);
}

/*****************************************************************************/
/*                           Private Wrapper funcs                           */
/*****************************************************************************/

static init_status_t
xine_player_init (player_t *player)
{
  xine_player_t *x = NULL;

  char *id_vo = NULL;
  char *id_ao = NULL;
  int use_x11 = 0;
  int visual = XINE_VISUAL_TYPE_NONE;
  void *data = NULL;
  char *homedir = getenv ("HOME");

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  x = player->priv;
  if (!x)
    return PLAYER_INIT_ERROR;

  x->xine = xine_new ();
  xine_config_load (x->xine, "/etc/xine/config");
  if (homedir)
  {
    size_t cfgfile_len = strlen (homedir) + strlen (".xine/config") + 2;
    char *cfgfile = malloc (cfgfile_len);
    snprintf (cfgfile, cfgfile_len, "%s/.xine/config", homedir);
    xine_config_load (x->xine, cfgfile);
    PFREE (cfgfile);
  }
  xine_init (x->xine);
  xine_engine_set_param (x->xine,
                         XINE_ENGINE_PARAM_VERBOSITY, XINE_VERBOSITY_LOG);

  switch (player->vo)
  {
  case PLAYER_VO_NULL:
    id_vo = "none";
    break;

#ifdef HAVE_WIN_XCB
  case PLAYER_VO_X11:
    use_x11 = 1;
    id_vo = "xshm";
    break;

  case PLAYER_VO_X11_SDL:
    use_x11 = 1;
    id_vo = "sdl";
    break;

  case PLAYER_VO_XV:
    use_x11 = 1;
    id_vo = "xv";
    break;

#ifdef USE_XLIB_HACK
  case PLAYER_VO_GL:
    use_x11 = 1;
    id_vo = "opengl";
    break;

  case PLAYER_VO_VDPAU:
    use_x11 = 1;
    id_vo = "vdpau";
    break;
#endif /* USE_XLIB_HACK */
#endif /* HAVE_WIN_XCB */

  case PLAYER_VO_FB:
    id_vo = "fb";
    visual = XINE_VISUAL_TYPE_FB;
    break;

  case PLAYER_VO_DIRECTFB:
    id_vo = "directfb";
    visual = XINE_VISUAL_TYPE_FB;
    break;

  case PLAYER_VO_AUTO:
    use_x11 = 1;
    break;

  default:
    pl_log (player, PLAYER_MSG_WARNING,
            MODULE_NAME, "Unsupported video output type");
    break;
  }

  if (use_x11)
  {
#ifdef HAVE_WIN_XCB
    int ret = pl_window_init (player->window);
    if (!ret && player->vo != PLAYER_VO_AUTO)
    {
      pl_log (player, PLAYER_MSG_ERROR,
              MODULE_NAME, "initialization for X has failed");
      return PLAYER_INIT_ERROR;
    }
    else if (!ret)
    {
      use_x11 = 0;
      visual = XINE_VISUAL_TYPE_FB;
    }
    else
    {
      data = pl_window_data_get (player->window);
#ifdef USE_XLIB_HACK
      visual = XINE_VISUAL_TYPE_X11;
#else /* USE_XLIB_HACK */
      visual = XINE_VISUAL_TYPE_XCB;
#endif /* !USE_XLIB_HACK */
    }
#else /* HAVE_WIN_XCB */
    pl_log (player, PLAYER_MSG_ERROR, MODULE_NAME,
            "auto-detection for window is not enabled without X11 support");
    return PLAYER_INIT_ERROR;
#endif /* !HAVE_WIN_XCB */
  }

  /* init video output driver */
  x->vo_port = xine_open_video_driver (x->xine, id_vo, visual, data);
  if (!x->vo_port)
  {
    pl_log (player, PLAYER_MSG_ERROR,
            MODULE_NAME, "xine can't init '%s' video driver",
            id_vo ? id_vo : "null");

    return PLAYER_INIT_ERROR;
  }

  switch (player->ao)
  {
  case PLAYER_AO_NULL:
    id_ao = "none";
    break;

  case PLAYER_AO_ALSA:
    id_ao = "alsa";
    break;

  case PLAYER_AO_OSS:
    id_ao = "oss";
    break;

  case PLAYER_AO_PULSE:
    id_ao = "pulseaudio";
    break;

  case PLAYER_AO_AUTO:
    break;

  default:
    pl_log (player, PLAYER_MSG_WARNING,
            MODULE_NAME, "Unsupported audio output type");
    break;
  }

  /* init audio output driver */
  x->ao_port = xine_open_audio_driver (x->xine, id_ao, NULL);
  if (!x->ao_port)
  {
    pl_log (player, PLAYER_MSG_ERROR,
            MODULE_NAME, "xine can't init '%s' audio driver",
            id_ao ? id_ao : "null");

    return PLAYER_INIT_ERROR;
  }

  x->stream = xine_stream_new (x->xine, x->ao_port, x->vo_port);

  x->event_queue = xine_event_new_queue (x->stream);
  xine_event_create_listener_thread (x->event_queue,
                                     xine_player_event_listener_cb, player);

#ifdef HAVE_WIN_XCB
  if (player->window)
    xine_port_send_gui_data (x->vo_port,
                             XINE_GUI_SEND_VIDEOWIN_VISIBLE, (void *) 1);
#endif /* HAVE_WIN_XCB */

  return PLAYER_INIT_OK;
}

static void
xine_player_uninit (player_t *player)
{
  xine_player_t *x = NULL;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "uninit");

  if (!player)
    return;

  x = player->priv;
  if (!x)
    return;

  if (x->stream)
  {
    xine_close (x->stream);
    xine_dispose (x->stream);
  }

  if (x->event_queue)
    xine_event_dispose_queue (x->event_queue);

  if (x->ao_port)
    xine_close_audio_driver (x->xine, x->ao_port);
  if (x->vo_port)
    xine_close_video_driver (x->xine, x->vo_port);

  if (x->xine)
    xine_exit (x->xine);

  pl_window_uninit (player->window);

  PFREE (x);
}

static void
xine_player_set_verbosity (player_t *player, player_verbosity_level_t level)
{
  xine_player_t *x;
  int verbosity = -1;

  if (!player)
    return;

  x = player->priv;
  if (!x)
    return;

  switch (level)
  {
  case PLAYER_MSG_NONE:
    verbosity = XINE_VERBOSITY_NONE;
    break;
  case PLAYER_MSG_VERBOSE:
  case PLAYER_MSG_INFO:
  case PLAYER_MSG_WARNING:
    verbosity = XINE_VERBOSITY_DEBUG;
    break;
  case PLAYER_MSG_ERROR:
  case PLAYER_MSG_CRITICAL:
    verbosity = XINE_VERBOSITY_LOG;
    break;
  default:
    break;
  }

  if (verbosity != -1)
    xine_engine_set_param (x->xine, XINE_ENGINE_PARAM_VERBOSITY, verbosity);
}

static void
xine_player_mrl_retrieve_properties (player_t *player, mrl_t *mrl)
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

  xine_identify (player, mrl,
                 IDENTIFY_AUDIO | IDENTIFY_VIDEO | IDENTIFY_PROPERTIES);
}

static void
xine_player_mrl_retrieve_metadata (player_t *player, mrl_t *mrl)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "mrl_retrieve_metadata");

  if (!player || !mrl || !mrl->meta)
    return;

  xine_identify (player, mrl, IDENTIFY_METADATA);
}

static int
xine_player_get_time_pos (player_t *player)
{
  xine_player_t *x;
  int time_pos = 0;
  int ret;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "get_time_pos");

  if (!player)
    return -1;

  x = player->priv;
  if (!x->stream)
    return -1;

  ret = xine_get_pos_length (x->stream, NULL, &time_pos, NULL);
  if (!ret || time_pos < 0)
    return -1;

  return time_pos;
}

static int
xine_player_get_percent_pos (player_t *player)
{
  xine_player_t *x;
  int percent_pos = 0;
  int ret;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "get_percent_pos");

  if (!player)
    return -1;

  x = player->priv;

  if (!x->stream)
    return -1;

  ret = xine_get_pos_length (x->stream, &percent_pos, NULL, NULL);
  if (!ret || percent_pos < 0)
    return -1;

  return percent_pos * 100 / (1 << 16);
}

static void
xine_player_set_mouse_pos (player_t *player, int x, int y)
{
  xine_player_t *xine;
  xine_input_data_t input;
  x11_rectangle_t rect;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "set_mouse_pos: %i %i", x, y);

  if (!player)
    return;

  xine = player->priv;

#ifdef HAVE_WIN_XCB
  if (player->window)
  {
    int video_x, video_y;

    pl_window_video_pos_get (player->window, &video_x, &video_y);
    x -= video_x;
    y -= video_y;
  }
#endif /* HAVE_WIN_XCB */

  rect.x = x;
  rect.y = y;
  rect.w = 0;
  rect.h = 0;
  xine_port_send_gui_data (xine->vo_port,
                           XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO, &rect);

  memset (&input, 0, sizeof (input));
  input.x = rect.x;
  input.y = rect.y;

  xine->mouse_x = rect.x;
  xine->mouse_y = rect.y;

  send_event (player, XINE_EVENT_INPUT_MOUSE_MOVE, &input, sizeof (input));
}

static playback_status_t
xine_player_playback_start (player_t *player)
{
  char *mrl = NULL;
  xine_player_t *x = NULL;
  char *uri = NULL;
  mrl_t *mrl_c;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_start");

  if (!player)
    return PLAYER_PB_FATAL;

  x = player->priv;

  mrl_c = pl_playlist_get_mrl (player->playlist);
  if (!x->stream || !mrl_c)
    return PLAYER_PB_ERROR;

  uri = xine_resource_get_uri (mrl_c);
  if (!uri)
    return PLAYER_PB_ERROR;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "uri: %s", uri);

  /* add subtitle to the MRL */
  if (mrl_c->subs)
  {
    mrl = malloc (strlen (uri) +
                  strlen (mrl_c->subs[0]) + 11);

    if (mrl)
      sprintf (mrl, "%s#subtitle:%s", uri, mrl_c->subs[0]);
  }
  /* or take only the name */
  else
    mrl = strdup (uri);

  PFREE (uri);

  if (!mrl)
    return PLAYER_PB_ERROR;

  if (MRL_USES_VO (mrl_c))
    pl_window_map (player->window);

  xine_open (x->stream, mrl);
  xine_play (x->stream, 0, 0);

  PFREE (mrl);

  return PLAYER_PB_OK;
}

static void
xine_player_playback_stop (player_t *player)
{
  mrl_t *mrl;
  xine_player_t *x = NULL;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_stop");

  if (!player)
    return;

  x = player->priv;

  if (!x->stream)
    return;

  mrl = pl_playlist_get_mrl (player->playlist);
  if (MRL_USES_VO (mrl))
    pl_window_unmap (player->window);

  xine_stop (x->stream);
  xine_close (x->stream);
}

static playback_status_t
xine_player_playback_pause (player_t *player)
{
  xine_player_t *x = NULL;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_pause");

  if (!player)
    return PLAYER_PB_FATAL;

  x = player->priv;

  if (!x->stream)
    return PLAYER_PB_ERROR;

  if (xine_get_param (x->stream, XINE_PARAM_SPEED) != XINE_SPEED_PAUSE)
    xine_set_param (x->stream, XINE_PARAM_SPEED, XINE_SPEED_PAUSE);
  else
    xine_set_param (x->stream, XINE_PARAM_SPEED, XINE_SPEED_NORMAL);

  return PLAYER_PB_OK;
}

static void
xine_player_playback_seek (player_t *player, int value, player_pb_seek_t seek)
{
  xine_player_t *x = NULL;
  int pos_time = 0, pos_percent = 0, length = 0;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "playback_seek: %d %d", value, seek);

  if (!player)
    return;

  x = player->priv;

  if (!x->stream)
    return;

  xine_get_pos_length (x->stream, NULL, &pos_time, &length);

  switch (seek)
  {
  default:
  case PLAYER_PB_SEEK_RELATIVE:
    pos_time += value;
    break;
  case PLAYER_PB_SEEK_PERCENT:
    pos_percent = (1 << 16) * value / 100;
    pos_time = 0;
    break;
  case PLAYER_PB_SEEK_ABSOLUTE:
    pos_time = value;
    break;
  }

  if (pos_time < 0)
    pos_time = 0;
  if (pos_time > length)
    pos_time = length;

  xine_play (x->stream, pos_percent, pos_time);
}

static void
xine_player_playback_set_speed (player_t *player, float value)
{
  int speed;
  xine_player_t *x = NULL;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "playback_set_speed: %f", value);

  if (!player)
    return;

  x = player->priv;

  if (!x->stream)
    return;

  speed = (int) (value * XINE_FINE_SPEED_NORMAL);
  xine_set_param (x->stream, XINE_PARAM_FINE_SPEED, speed);
}

static int
xine_player_audio_get_volume (player_t *player)
{
  xine_player_t *x = NULL;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "audio_get_volume");

  if (!player)
    return -1;

  x = player->priv;

  if (!x->stream)
    return -1;

  return xine_get_param (x->stream, XINE_PARAM_AUDIO_VOLUME);
}

static void
xine_player_audio_set_volume (player_t *player, int value)
{
  xine_player_t *x = NULL;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "audio_set_volume: %d", value);

  if (!player)
    return;

  x = player->priv;

  if (!x->stream)
    return;

  xine_set_param (x->stream, XINE_PARAM_AUDIO_VOLUME, value);
}

static player_mute_t
xine_player_audio_get_mute (player_t *player)
{
  xine_player_t *x = NULL;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "audio_get_mute");

  if (!player)
    return PLAYER_MUTE_UNKNOWN;

  x = player->priv;

  if (!x->stream)
    return PLAYER_MUTE_UNKNOWN;

  if (xine_get_param (x->stream, XINE_PARAM_AUDIO_MUTE))
    return PLAYER_MUTE_ON;

  return PLAYER_MUTE_OFF;
}

static void
xine_player_audio_set_mute (player_t *player, player_mute_t value)
{
  xine_player_t *x = NULL;
  int mute = 0;

  if (value == PLAYER_MUTE_UNKNOWN)
    return;

  if (value == PLAYER_MUTE_ON)
    mute = 1;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "audio_set_mute: %s", mute ? "on" : "off");

  if (!player)
    return;

  x = player->priv;

  if (!x->stream)
    return;

  xine_set_param (x->stream, XINE_PARAM_AUDIO_MUTE, mute);
}

static void
xine_player_video_set_ar (player_t *player, float value)
{
  xine_player_t *x;
  mrl_t *mrl;
  int ar;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "video_set_ar: %.2f", value);

  if (!player)
    return;

  x = player->priv;

  if (!x->stream)
    return;

  mrl = pl_playlist_get_mrl (player->playlist);
  if (!MRL_USES_VO (mrl))
    return;

  /* use original aspect ratio if value is 0.0 */
  if (!value)
  {
    mrl_properties_video_t *video = mrl->prop->video;
    player->aspect = video->aspect / PLAYER_VIDEO_ASPECT_RATIO_MULT;
    ar = XINE_VO_ASPECT_AUTO;
  }
  else
  {
    player->aspect = value;
    if (value >= 2.11)
      ar = XINE_VO_ASPECT_DVB;
    else if (value >= 16.0 / 9.0)
      ar = XINE_VO_ASPECT_ANAMORPHIC;
    else if (value >= 4.0 / 3.0)
      ar = XINE_VO_ASPECT_4_3;
    else if (value >= 1.0)
      ar = XINE_VO_ASPECT_SQUARE;
    else
      return;
  }

  xine_set_param (x->stream, XINE_PARAM_VO_ASPECT_RATIO, ar);
}

static void
xine_player_sub_set_delay (player_t *player, int value)
{
  int delay;
  xine_player_t *x = NULL;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "sub_set_delay: %i", value);

  /* unit is 1/90000 sec */
  delay = (int) rintf (value / 1000.0 * PLAYER_VIDEO_FRAMEDURATION_RATIO_DIV);

  if (!player)
    return;

  x = player->priv;

  if (!x->stream)
    return;

  xine_set_param (x->stream, XINE_PARAM_SPU_OFFSET, delay);
}

static void
xine_player_dvd_nav (player_t *player, player_dvdnav_t value)
{
  int event;
  xine_player_t *x;
  xine_input_data_t *input = NULL;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "dvd_nav: %i", value);

  if (!player)
    return;

  x = player->priv;

  switch (value)
  {
  case PLAYER_DVDNAV_UP:
    event = XINE_EVENT_INPUT_UP;
    break;

  case PLAYER_DVDNAV_DOWN:
    event = XINE_EVENT_INPUT_DOWN;
    break;

  case PLAYER_DVDNAV_LEFT:
    event = XINE_EVENT_INPUT_LEFT;
    break;

  case PLAYER_DVDNAV_RIGHT:
    event = XINE_EVENT_INPUT_RIGHT;
    break;

  case PLAYER_DVDNAV_MENU:
    /* go to root menu if possible */
    event = XINE_EVENT_INPUT_MENU3;
    break;

  case PLAYER_DVDNAV_SELECT:
    event = XINE_EVENT_INPUT_SELECT;
    break;

  case PLAYER_DVDNAV_MOUSECLICK:
    event = XINE_EVENT_INPUT_MOUSE_BUTTON;
    input = PCALLOC (xine_input_data_t, 1);
    if (!input)
      break;

    input->button = 1;
    input->x = x->mouse_x;
    input->y = x->mouse_y;
    break;

  default:
    return;
  }

  send_event (player, event, input, input ? sizeof (*input) : 0);

  PFREE (input);
}

static void
xine_player_vdr (player_t *player, player_vdr_t value)
{
  static const player_vdr_t events[] = {
    [PLAYER_VDR_UP]               = XINE_EVENT_INPUT_UP,
    [PLAYER_VDR_DOWN]             = XINE_EVENT_INPUT_DOWN,
    [PLAYER_VDR_LEFT]             = XINE_EVENT_INPUT_LEFT,
    [PLAYER_VDR_RIGHT]            = XINE_EVENT_INPUT_RIGHT,
    [PLAYER_VDR_OK]               = XINE_EVENT_INPUT_SELECT,
    [PLAYER_VDR_BACK]             = XINE_EVENT_VDR_BACK,
    [PLAYER_VDR_CHANNELPLUS]      = XINE_EVENT_VDR_CHANNELPLUS,
    [PLAYER_VDR_CHANNELMINUS]     = XINE_EVENT_VDR_CHANNELMINUS,
    [PLAYER_VDR_RED]              = XINE_EVENT_VDR_RED,
    [PLAYER_VDR_GREEN]            = XINE_EVENT_VDR_GREEN,
    [PLAYER_VDR_YELLOW]           = XINE_EVENT_VDR_YELLOW,
    [PLAYER_VDR_BLUE]             = XINE_EVENT_VDR_BLUE,
    [PLAYER_VDR_PLAY]             = XINE_EVENT_VDR_PLAY,
    [PLAYER_VDR_PAUSE]            = XINE_EVENT_VDR_PAUSE,
    [PLAYER_VDR_STOP]             = XINE_EVENT_VDR_STOP,
    [PLAYER_VDR_RECORD]           = XINE_EVENT_VDR_RECORD,
    [PLAYER_VDR_FASTFWD]          = XINE_EVENT_VDR_FASTFWD,
    [PLAYER_VDR_FASTREW]          = XINE_EVENT_VDR_FASTREW,
    [PLAYER_VDR_POWER]            = XINE_EVENT_VDR_POWER,
    [PLAYER_VDR_SCHEDULE]         = XINE_EVENT_VDR_SCHEDULE,
    [PLAYER_VDR_CHANNELS]         = XINE_EVENT_VDR_CHANNELS,
    [PLAYER_VDR_TIMERS]           = XINE_EVENT_VDR_TIMERS,
    [PLAYER_VDR_RECORDINGS]       = XINE_EVENT_VDR_RECORDINGS,
    [PLAYER_VDR_MENU]             = XINE_EVENT_INPUT_MENU1,
    [PLAYER_VDR_SETUP]            = XINE_EVENT_VDR_SETUP,
    [PLAYER_VDR_COMMANDS]         = XINE_EVENT_VDR_COMMANDS,
    [PLAYER_VDR_0]                = XINE_EVENT_INPUT_NUMBER_0,
    [PLAYER_VDR_1]                = XINE_EVENT_INPUT_NUMBER_1,
    [PLAYER_VDR_2]                = XINE_EVENT_INPUT_NUMBER_2,
    [PLAYER_VDR_3]                = XINE_EVENT_INPUT_NUMBER_3,
    [PLAYER_VDR_4]                = XINE_EVENT_INPUT_NUMBER_4,
    [PLAYER_VDR_5]                = XINE_EVENT_INPUT_NUMBER_5,
    [PLAYER_VDR_6]                = XINE_EVENT_INPUT_NUMBER_6,
    [PLAYER_VDR_7]                = XINE_EVENT_INPUT_NUMBER_7,
    [PLAYER_VDR_8]                = XINE_EVENT_INPUT_NUMBER_8,
    [PLAYER_VDR_9]                = XINE_EVENT_INPUT_NUMBER_9,
    [PLAYER_VDR_USER_1]           = XINE_EVENT_VDR_USER1,
    [PLAYER_VDR_USER_2]           = XINE_EVENT_VDR_USER2,
    [PLAYER_VDR_USER_3]           = XINE_EVENT_VDR_USER3,
    [PLAYER_VDR_USER_4]           = XINE_EVENT_VDR_USER4,
    [PLAYER_VDR_USER_5]           = XINE_EVENT_VDR_USER5,
    [PLAYER_VDR_USER_6]           = XINE_EVENT_VDR_USER6,
    [PLAYER_VDR_USER_7]           = XINE_EVENT_VDR_USER7,
    [PLAYER_VDR_USER_8]           = XINE_EVENT_VDR_USER8,
    [PLAYER_VDR_USER_9]           = XINE_EVENT_VDR_USER9,
    [PLAYER_VDR_VOLPLUS]          = XINE_EVENT_VDR_VOLPLUS,
    [PLAYER_VDR_VOLMINUS]         = XINE_EVENT_VDR_VOLMINUS,
    [PLAYER_VDR_MUTE]             = XINE_EVENT_VDR_MUTE,
    [PLAYER_VDR_AUDIO]            = XINE_EVENT_VDR_AUDIO,
    [PLAYER_VDR_INFO]             = XINE_EVENT_VDR_INFO,
    [PLAYER_VDR_CHANNELPREVIOUS]  = XINE_EVENT_VDR_CHANNELPREVIOUS,
    [PLAYER_VDR_NEXT]             = XINE_EVENT_INPUT_NEXT,
    [PLAYER_VDR_PREVIOUS]         = XINE_EVENT_INPUT_PREVIOUS,
    [PLAYER_VDR_SUBTITLES]        = XINE_EVENT_VDR_SUBTITLES,
  };

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "vdr: %u", value);

  if (!player)
    return;

  if (value >= ARRAY_NB_ELEMENTS (events))
    return;

  send_event (player, events[value], NULL, 0);
}

/*****************************************************************************/
/*                            Public Wrapper API                             */
/*****************************************************************************/

int
pl_supported_resources_xine (mrl_resource_t res)
{
  switch (res)
  {
  case MRL_RESOURCE_DVD:
  case MRL_RESOURCE_DVDNAV:
  case MRL_RESOURCE_FILE:
  case MRL_RESOURCE_HTTP:
  case MRL_RESOURCE_MMS:
  case MRL_RESOURCE_NETVDR:
  case MRL_RESOURCE_RTP:
  case MRL_RESOURCE_TCP:
  case MRL_RESOURCE_UDP:
  case MRL_RESOURCE_VDR:
    return 1;

  default:
    return 0;
  }
}

player_funcs_t *
pl_register_functions_xine (void)
{
  player_funcs_t *funcs = NULL;

  funcs = PCALLOC (player_funcs_t, 1);
  if (!funcs)
    return NULL;

  funcs->init               = xine_player_init;
  funcs->uninit             = xine_player_uninit;
  funcs->set_verbosity      = xine_player_set_verbosity;

  funcs->mrl_retrieve_props = xine_player_mrl_retrieve_properties;
  funcs->mrl_retrieve_meta  = xine_player_mrl_retrieve_metadata;
  funcs->mrl_video_snapshot = NULL;

  funcs->get_time_pos       = xine_player_get_time_pos;
  funcs->get_percent_pos    = xine_player_get_percent_pos;
  funcs->set_framedrop      = NULL;
  funcs->set_mouse_pos      = xine_player_set_mouse_pos;
  funcs->osd_show_text      = NULL;
  funcs->osd_state          = NULL;

  funcs->pb_start           = xine_player_playback_start;
  funcs->pb_stop            = xine_player_playback_stop;
  funcs->pb_pause           = xine_player_playback_pause;
  funcs->pb_seek            = xine_player_playback_seek;
  funcs->pb_seek_chapter    = NULL;
  funcs->pb_set_speed       = xine_player_playback_set_speed;

  funcs->audio_get_volume   = xine_player_audio_get_volume;
  funcs->audio_set_volume   = xine_player_audio_set_volume;
  funcs->audio_get_mute     = xine_player_audio_get_mute;
  funcs->audio_set_mute     = xine_player_audio_set_mute;
  funcs->audio_set_delay    = NULL;
  funcs->audio_select       = NULL;
  funcs->audio_prev         = NULL;
  funcs->audio_next         = NULL;

  funcs->video_set_aspect   = NULL;
  funcs->video_set_panscan  = NULL;
  funcs->video_set_ar       = xine_player_video_set_ar;

  funcs->sub_set_delay      = xine_player_sub_set_delay;
  funcs->sub_set_alignment  = NULL;
  funcs->sub_set_pos        = NULL;
  funcs->sub_set_visibility = NULL;
  funcs->sub_scale          = NULL;
  funcs->sub_select         = NULL;
  funcs->sub_prev           = NULL;
  funcs->sub_next           = NULL;

  funcs->dvd_nav            = xine_player_dvd_nav;
  funcs->dvd_angle_set      = NULL;
  funcs->dvd_angle_prev     = NULL;
  funcs->dvd_angle_next     = NULL;
  funcs->dvd_title_set      = NULL;
  funcs->dvd_title_prev     = NULL;
  funcs->dvd_title_next     = NULL;

  funcs->tv_channel_set     = NULL;
  funcs->tv_channel_prev    = NULL;
  funcs->tv_channel_next    = NULL;

  funcs->radio_channel_set  = PL_NOT_SUPPORTED;
  funcs->radio_channel_prev = PL_NOT_SUPPORTED;
  funcs->radio_channel_next = PL_NOT_SUPPORTED;

  funcs->vdr                = xine_player_vdr;

  return funcs;
}

void *
pl_register_private_xine (void)
{
  xine_player_t *x = NULL;

  x = PCALLOC (xine_player_t, 1);
  if (!x)
    return NULL;

  return x;
}
