/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006-2008 Benjamin Zores <ben@geexbox.org>
 * Copyright (C) 2007-2009 Mathieu Schroeter <mathieu.schroeter@gamesover.ch>
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
#include "wrapper_xine.h"

#ifdef USE_X11
#include "x11_common.h"
#endif /* USE_X11 */

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

  player = (player_t *) user_data;
  if (!player)
    return;

  switch (event->type)
  {
  case XINE_EVENT_UI_PLAYBACK_FINISHED:
  {
    pl_log (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Playback of stream has ended");
    player_event_send (player, PLAYER_EVENT_PLAYBACK_FINISHED, NULL);

#ifdef USE_X11
    if (player->x11)
      pl_x11_unmap (player);
#endif /* USE_X11 */
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

  x = (xine_player_t *) player->priv;

  if (!x || !x->stream)
    return;

  xine_event.type = event;
  xine_event.stream = x->stream;
  xine_event.data = data;
  xine_event.data_length = data_size;

  xine_event_send (x->stream, &xine_event);
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
    [MRL_RESOURCE_NETVDR]   = "netvdr://",

    [MRL_RESOURCE_UNKNOWN]  = NULL
  };

  if (!mrl)
    return NULL;

  switch (mrl->resource)
  {
  case MRL_RESOURCE_FILE: /* file:location */
  {
    const char *protocol = protocols[mrl->resource];
    mrl_resource_local_args_t *args = mrl->priv;

    if (!args || !args->location)
      return NULL;

    if (strchr (args->location, ':')
        && strncmp (args->location, protocol, strlen (protocol)))
    {
      return NULL;
    }

    return strdup (args->location);
  }

  case MRL_RESOURCE_DVD:    /* dvd:device/title_start */
  case MRL_RESOURCE_DVDNAV: /* dvd:device/title_start */
  {
    char *uri;
    const char *protocol = protocols[mrl->resource];
    char title_start[8] = "";
    size_t size = strlen (protocol);
    mrl_resource_videodisc_args_t *args;

    args = mrl->priv;
    if (!args)
      break;

    if (args->device)
      size += strlen (args->device);

    if (args->title_start)
    {
      size += 1 + pl_count_nb_dec (args->title_start);
      snprintf (title_start, sizeof (title_start), "/%i", args->title_start);
    }

    size++;
    uri = malloc (size);
    if (!uri)
      break;

    snprintf (uri, size, "%s%s%s",
              protocol, args->device ? args->device : "", title_start);

    return uri;
  }

  case MRL_RESOURCE_VDR: /* vdr:/device#driver */
  {
    char *uri;
    const char *protocol = protocols[mrl->resource];
    size_t size = strlen (protocol);
    mrl_resource_tv_args_t *args = mrl->priv;

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

  case MRL_RESOURCE_NETVDR: /* netvdr://host:port */
  {
    const char *protocol = protocols[mrl->resource];
    mrl_resource_network_args_t *args = mrl->priv;

    if (!args || !args->url)
      return NULL;

    if (strncmp (args->url, protocol, strlen (protocol)))
      return NULL;

    return strdup (args->url);
  }

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
    if (dvd->volumeid)
      free (dvd->volumeid);
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
    if (meta->title)
      free (meta->title);
    meta->title = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_ARTIST);
  if (s)
  {
    if (meta->artist)
      free (meta->artist);
    meta->artist = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_GENRE);
  if (s)
  {
    if (meta->genre)
      free (meta->genre);
    meta->genre = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_ALBUM);
  if (s)
  {
    if (meta->album)
      free (meta->album);
    meta->album = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_YEAR);
  if (s)
  {
    if (meta->year)
      free (meta->year);
    meta->year = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_TRACK_NUMBER);
  if (s)
  {
    if (meta->track)
      free (meta->track);
    meta->track = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_COMMENT);
  if (s)
  {
    if (meta->comment)
      free (meta->comment);
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
    if (audio->codec)
      free (audio->codec);
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
    if (video->codec)
      free (video->codec);
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

  x = (xine_player_t *) player->priv;

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
  free (uri);
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

  x = (xine_player_t *) player->priv;

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
    free (cfgfile);
  }
  xine_init (x->xine);
  xine_engine_set_param (x->xine,
                         XINE_ENGINE_PARAM_VERBOSITY, XINE_VERBOSITY_LOG);

  switch (player->vo)
  {
  case PLAYER_VO_NULL:
    id_vo = "none";
    break;

#ifdef USE_X11
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

  case PLAYER_VO_GL:
    use_x11 = 1;
    id_vo = "opengl";
    break;
#endif /* USE_X11 */

  case PLAYER_VO_FB:
    id_vo = "fb";
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
#ifdef USE_X11
    int ret = pl_x11_init (player);
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
      data = pl_x11_get_data (player->x11);
      visual = XINE_VISUAL_TYPE_XCB;
    }
#else
    pl_log (player, PLAYER_MSG_ERROR, MODULE_NAME,
            "auto-detection for videoout is not enabled without X11 support");
    return PLAYER_INIT_ERROR;
#endif /* USE_X11 */
  }

  /* init video output driver */
  if (!(x->vo_port = xine_open_video_driver (x->xine, id_vo, visual, data)))
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

  case PLAYER_AO_AUTO:
    break;

  default:
    pl_log (player, PLAYER_MSG_WARNING,
            MODULE_NAME, "Unsupported audio output type");
    break;
  }

  /* init audio output driver */
  if (!(x->ao_port = xine_open_audio_driver (x->xine, id_ao, NULL)))
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

#ifdef USE_X11
  if (player->x11)
    xine_port_send_gui_data (x->vo_port, XINE_GUI_SEND_VIDEOWIN_VISIBLE,
                             (void *) 1);
#endif /* USE_X11 */

  return PLAYER_INIT_OK;
}

static void
xine_player_uninit (player_t *player)
{
  xine_player_t *x = NULL;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "uninit");

  if (!player)
    return;

  x = (xine_player_t *) player->priv;

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

#ifdef USE_X11
  if (player->x11)
    pl_x11_uninit (player);
#endif /* USE_X11 */

  free (x);
}

static void
xine_player_set_verbosity (player_t *player, player_verbosity_level_t level)
{
  xine_player_t *x;
  int verbosity = -1;

  if (!player)
    return;

  x = (xine_player_t *) player->priv;
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

  x = (xine_player_t *) player->priv;

  if (!x || !x->stream)
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

#ifdef USE_X11
  if (player->x11)
  {
    int video_x, video_y;

    pl_x11_get_video_pos (player->x11, &video_x, &video_y);
    x -= video_x;
    y -= video_y;
  }
#endif /* USE_X11 */

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

  x = (xine_player_t *) player->priv;

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

  free (uri);

  if (!mrl)
    return PLAYER_PB_ERROR;

#ifdef USE_X11
  if (player->x11 && !mrl_uses_vo (mrl_c))
    pl_x11_map (player);
#endif /* USE_X11 */

  xine_open (x->stream, mrl);
  xine_play (x->stream, 0, 0);

  free (mrl);

  return PLAYER_PB_OK;
}

static void
xine_player_playback_stop (player_t *player)
{
#ifdef USE_X11
  mrl_t *mrl;
#endif /* USE_X11 */
  xine_player_t *x = NULL;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_stop");

  if (!player)
    return;

  x = (xine_player_t *) player->priv;

  if (!x->stream)
    return;

#ifdef USE_X11
  mrl = pl_playlist_get_mrl (player->playlist);
  if (player->x11 && !mrl_uses_vo (mrl))
    pl_x11_unmap (player);
#endif /* USE_X11 */

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

  x = (xine_player_t *) player->priv;

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

  x = (xine_player_t *) player->priv;

  if (!x->stream)
    return;

  xine_get_pos_length (x->stream, NULL, &pos_time, &length);

  switch (seek)
  {
  default:
  case PLAYER_PB_SEEK_RELATIVE:
    pos_time += value * 1000;
    break;
  case PLAYER_PB_SEEK_PERCENT:
    pos_percent = (1 << 16) * value / 100;
    pos_time = 0;
    break;
  case PLAYER_PB_SEEK_ABSOLUTE:
    pos_time = value * 1000;
    break;
  }

  if (pos_time < 0)
    pos_time = 0;
  if (pos_time > length)
    pos_time = length;

  xine_play (x->stream, pos_percent, pos_time);
}

static int
xine_player_audio_get_volume (player_t *player)
{
  xine_player_t *x = NULL;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "audio_get_volume");

  if (!player)
    return -1;

  x = (xine_player_t *) player->priv;

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

  x = (xine_player_t *) player->priv;

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

  x = (xine_player_t *) player->priv;

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

  x = (xine_player_t *) player->priv;

  if (!x->stream)
    return;

  xine_set_param (x->stream, XINE_PARAM_AUDIO_MUTE, mute);
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

  x = (xine_player_t *) player->priv;

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
    input = calloc (1, sizeof (xine_input_data_t));
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

  if (input)
    free (input);
}

static void
xine_player_vdr (player_t *player, player_vdr_t value)
{
  int event;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "vdr: %i", value);

  if (!player)
    return;

  switch (value)
  {
  case PLAYER_VDR_UP:
    event = XINE_EVENT_INPUT_UP;
    break;

  case PLAYER_VDR_DOWN:
    event = XINE_EVENT_INPUT_DOWN;
    break;

  case PLAYER_VDR_LEFT:
    event = XINE_EVENT_INPUT_LEFT;
    break;

  case PLAYER_VDR_RIGHT:
    event = XINE_EVENT_INPUT_RIGHT;
    break;

  case PLAYER_VDR_OK:
    event = XINE_EVENT_INPUT_SELECT;
    break;

  case PLAYER_VDR_BACK:
    event = XINE_EVENT_VDR_BACK;
    break;

  case PLAYER_VDR_CHANNELPLUS:
    event = XINE_EVENT_VDR_CHANNELPLUS;
    break;

  case PLAYER_VDR_CHANNELMINUS:
    event = XINE_EVENT_VDR_CHANNELMINUS;
    break;

  case PLAYER_VDR_RED:
    event = XINE_EVENT_VDR_RED;
    break;

  case PLAYER_VDR_GREEN:
    event = XINE_EVENT_VDR_GREEN;
    break;

  case PLAYER_VDR_YELLOW:
    event = XINE_EVENT_VDR_YELLOW;
    break;

  case PLAYER_VDR_BLUE:
    event = XINE_EVENT_VDR_BLUE;
    break;

  case PLAYER_VDR_PLAY:
    event = XINE_EVENT_VDR_PLAY;
    break;

  case PLAYER_VDR_PAUSE:
    event = XINE_EVENT_VDR_PAUSE;
    break;

  case PLAYER_VDR_STOP:
    event = XINE_EVENT_VDR_STOP;
    break;

  case PLAYER_VDR_RECORD:
    event = XINE_EVENT_VDR_RECORD;
    break;

  case PLAYER_VDR_FASTFWD:
    event = XINE_EVENT_VDR_FASTFWD;
    break;

  case PLAYER_VDR_FASTREW:
    event = XINE_EVENT_VDR_FASTREW;
    break;

  case PLAYER_VDR_POWER:
    event = XINE_EVENT_VDR_POWER;
    break;

  case PLAYER_VDR_SCHEDULE:
    event = XINE_EVENT_VDR_SCHEDULE;
    break;

  case PLAYER_VDR_CHANNELS:
    event = XINE_EVENT_VDR_CHANNELS;
    break;

  case PLAYER_VDR_TIMERS:
    event = XINE_EVENT_VDR_TIMERS;
    break;

  case PLAYER_VDR_RECORDINGS:
    event = XINE_EVENT_VDR_RECORDINGS;
    break;

  case PLAYER_VDR_MENU:
    event = XINE_EVENT_INPUT_MENU1;
    break;

  case PLAYER_VDR_SETUP:
    event = XINE_EVENT_VDR_SETUP;
    break;

  case PLAYER_VDR_COMMANDS:
    event = XINE_EVENT_VDR_COMMANDS;
    break;

  case PLAYER_VDR_0:
    event = XINE_EVENT_INPUT_NUMBER_0;
    break;

  case PLAYER_VDR_1:
    event = XINE_EVENT_INPUT_NUMBER_1;
    break;

  case PLAYER_VDR_2:
    event = XINE_EVENT_INPUT_NUMBER_2;
    break;

  case PLAYER_VDR_3:
    event = XINE_EVENT_INPUT_NUMBER_3;
    break;

  case PLAYER_VDR_4:
    event = XINE_EVENT_INPUT_NUMBER_4;
    break;

  case PLAYER_VDR_5:
    event = XINE_EVENT_INPUT_NUMBER_5;
    break;

  case PLAYER_VDR_6:
    event = XINE_EVENT_INPUT_NUMBER_6;
    break;

  case PLAYER_VDR_7:
    event = XINE_EVENT_INPUT_NUMBER_7;
    break;

  case PLAYER_VDR_8:
    event = XINE_EVENT_INPUT_NUMBER_8;
    break;

  case PLAYER_VDR_9:
    event = XINE_EVENT_INPUT_NUMBER_9;
    break;

  case PLAYER_VDR_USER_1:
    event = XINE_EVENT_VDR_USER1;
    break;

  case PLAYER_VDR_USER_2:
    event = XINE_EVENT_VDR_USER2;
    break;

  case PLAYER_VDR_USER_3:
    event = XINE_EVENT_VDR_USER3;
    break;

  case PLAYER_VDR_USER_4:
    event = XINE_EVENT_VDR_USER4;
    break;

  case PLAYER_VDR_USER_5:
    event = XINE_EVENT_VDR_USER5;
    break;

  case PLAYER_VDR_USER_6:
    event = XINE_EVENT_VDR_USER6;
    break;

  case PLAYER_VDR_USER_7:
    event = XINE_EVENT_VDR_USER7;
    break;

  case PLAYER_VDR_USER_8:
    event = XINE_EVENT_VDR_USER8;
    break;

  case PLAYER_VDR_USER_9:
    event = XINE_EVENT_VDR_USER9;
    break;

  case PLAYER_VDR_VOLPLUS:
    event = XINE_EVENT_VDR_VOLPLUS;
    break;

  case PLAYER_VDR_VOLMINUS:
    event = XINE_EVENT_VDR_VOLMINUS;
    break;

  case PLAYER_VDR_MUTE:
    event = XINE_EVENT_VDR_MUTE;
    break;

  case PLAYER_VDR_AUDIO:
    event = XINE_EVENT_VDR_AUDIO;
    break;

  case PLAYER_VDR_INFO:
    event = XINE_EVENT_VDR_INFO;
    break;

  case PLAYER_VDR_CHANNELPREVIOUS:
    event = XINE_EVENT_VDR_CHANNELPREVIOUS;
    break;

  case PLAYER_VDR_NEXT:
    event = XINE_EVENT_INPUT_NEXT;
    break;

  case PLAYER_VDR_PREVIOUS:
    event = XINE_EVENT_INPUT_PREVIOUS;
    break;

  case PLAYER_VDR_SUBTITLES:
    event = XINE_EVENT_VDR_SUBTITLES;
    break;

  default:
    return;
  }

  send_event (player, event, NULL, 0);
}

/*****************************************************************************/
/*                            Public Wrapper API                             */
/*****************************************************************************/

int
pl_supported_resources_xine (mrl_resource_t res)
{
  switch (res)
  {
  case MRL_RESOURCE_FILE:
  case MRL_RESOURCE_DVD:
  case MRL_RESOURCE_DVDNAV:
  case MRL_RESOURCE_VDR:
  case MRL_RESOURCE_NETVDR:
    return 1;

  default:
    return 0;
  }
}

player_funcs_t *
pl_register_functions_xine (void)
{
  player_funcs_t *funcs = NULL;

  funcs = calloc (1, sizeof (player_funcs_t));
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

  funcs->pb_start           = xine_player_playback_start;
  funcs->pb_stop            = xine_player_playback_stop;
  funcs->pb_pause           = xine_player_playback_pause;
  funcs->pb_seek            = xine_player_playback_seek;
  funcs->pb_seek_chapter    = NULL;
  funcs->pb_set_speed       = NULL;

  funcs->audio_get_volume   = xine_player_audio_get_volume;
  funcs->audio_set_volume   = xine_player_audio_set_volume;
  funcs->audio_get_mute     = xine_player_audio_get_mute;
  funcs->audio_set_mute     = xine_player_audio_set_mute;
  funcs->audio_set_delay    = NULL;
  funcs->audio_select       = NULL;
  funcs->audio_prev         = NULL;
  funcs->audio_next         = NULL;

  funcs->video_set_fs       = NULL;
  funcs->video_set_aspect   = NULL;
  funcs->video_set_panscan  = NULL;
  funcs->video_set_ar       = NULL;

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

  funcs->radio_channel_set  = NULL;
  funcs->radio_channel_prev = NULL;
  funcs->radio_channel_next = NULL;

  funcs->vdr                = xine_player_vdr;

  return funcs;
}

void *
pl_register_private_xine (void)
{
  xine_player_t *x = NULL;

  x = calloc (1, sizeof (xine_player_t));
  if (!x)
    return NULL;

  x->vo_port = PLAYER_VO_NULL;
  x->ao_port = PLAYER_AO_NULL;

  return x;
}
