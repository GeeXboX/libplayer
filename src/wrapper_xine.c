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

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <xine.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "wrapper_xine.h"

#define MODULE_NAME "xine"

/* player specific structure */
typedef struct xine_player_s {
  xine_t *xine;
  xine_stream_t *stream;
  xine_event_queue_t *event_queue;
  xine_video_port_t *vo_port;
  xine_audio_port_t *ao_port;
  /* X11 */
  Display *display;
  Window window;
} xine_player_t;

/* for no border with a X window */
typedef struct {
  uint32_t flags;
  uint32_t functions;
  uint32_t decorations;
  int32_t input_mode;
  uint32_t status;
} MWMHints;

#define MWM_HINTS_DECORATIONS     (1L << 1)
#define PROP_MWM_HINTS_ELEMENTS   5

/* width and height for the fullscreen */
static int g_x11_width;
static int g_x11_height;
static double g_x11_pixel_aspect;

static void
x11_map (player_t *player)
{
  xine_player_t *x = NULL;

  if (!player)
    return;

  x = (xine_player_t *) player->priv;

  if (!x || !x->display)
    return;

  XLockDisplay (x->display);
  XMapRaised (x->display, x->window);
  XSync (x->display, False);
  XUnlockDisplay (x->display);
}

static void
x11_unmap (player_t *player)
{
  xine_player_t *x = NULL;

  if (!player)
    return;

  x = (xine_player_t *) player->priv;

  if (!x || !x->display)
    return;

  XLockDisplay (x->display);
  XUnmapWindow (x->display, x->window);
  XSync (x->display, False);
  XUnlockDisplay (x->display);
}

static void
xine_player_event_listener_cb (void *user_data, const xine_event_t *event)
{
  player_t *player = NULL;
  xine_player_t *x = NULL;

  player = (player_t *) user_data;
  if (!player)
    return;

  x = (xine_player_t *) player->priv;

  switch (event->type)
  {
  case XINE_EVENT_UI_PLAYBACK_FINISHED:
  {
    plog (MODULE_NAME, "Playback of stream has ended"); 
    if (player->event_cb)
      player->event_cb (PLAYER_EVENT_PLAYBACK_FINISHED, NULL);
    /* X11 */
    if (x && x->display && player->mrl->type == PLAYER_MRL_TYPE_VIDEO)
      x11_unmap (player);
    break;
  }
  case XINE_EVENT_PROGRESS:
  {
    xine_progress_data_t *pevent = (xine_progress_data_t *) event->data;
    plog (MODULE_NAME, "%s [%d%%]", pevent->description, pevent->percent);
    break;
  }
  }
}

/* private functions */
static void
dest_size_cb(void *data, int video_width, int video_height,
             double video_pixel_aspect, int *dest_width,
             int *dest_height, double *dest_pixel_aspect)
{
  *dest_width = g_x11_width;
  *dest_height = g_x11_height;
  *dest_pixel_aspect = g_x11_pixel_aspect;
}

static void
frame_output_cb(void *data, int video_width, int video_height,
                double video_pixel_aspect, int *dest_x, int *dest_y,
                int *dest_width, int *dest_height,
                double *dest_pixel_aspect, int *win_x, int *win_y)
{
  *dest_x = 0;
  *dest_y = 0;
  *win_x = 0;
  *win_y = 0;
  *dest_width = g_x11_width;
  *dest_height = g_x11_height;
  *dest_pixel_aspect = g_x11_pixel_aspect;
}

static x11_visual_t *
x11_init (player_t *player)
{
  xine_player_t *x = NULL;
  x11_visual_t *vis = NULL;
  int screen;
  double res_v, res_h;
  Atom XA_NO_BORDER;
  MWMHints mwmhints;
  XSetWindowAttributes atts;

  if (!player)
    return NULL;

  x = (xine_player_t *) player->priv;

  if (!x)
    return NULL;

  if (!XInitThreads ()) {
    plog (MODULE_NAME, "Failed to init for X11");
    return NULL;
  }

  x->display = XOpenDisplay (NULL);
  screen = XDefaultScreen (x->display);

  /* the video will be in fullscreen */
  g_x11_width = DisplayWidth (x->display, screen);
  g_x11_height = DisplayHeight (x->display, screen);

  XLockDisplay (x->display);

  atts.override_redirect = True;  /* window on top */
  atts.background_pixel = 0;      /* black background */

  /* create a window for the xine vo */
  x->window = XCreateWindow (x->display, DefaultRootWindow (x->display),
                             0, 0, g_x11_width, g_x11_height, 0, 0,
                             InputOutput, DefaultVisual (x->display, screen),
                             CWOverrideRedirect | CWBackPixel, &atts);

  /* remove borders for the fullscreen */
  XA_NO_BORDER = XInternAtom (x->display, "_MOTIF_WM_HINTS", False);
  mwmhints.flags = MWM_HINTS_DECORATIONS;
  mwmhints.decorations = 0;
  XChangeProperty (x->display, x->window, XA_NO_BORDER, XA_NO_BORDER, 32,
                   PropModeReplace, (unsigned char *) &mwmhints,
                   PROP_MWM_HINTS_ELEMENTS);

  /* calcul pixel aspect */
  res_h = DisplayWidth (x->display, screen) * 1000 /
          DisplayWidthMM (x->display, screen);
  res_v = DisplayHeight (x->display, screen) * 1000 /
          DisplayHeightMM (x->display, screen);
  g_x11_pixel_aspect = res_v / res_h;

  XSync (x->display, False);
  XUnlockDisplay (x->display);

  vis = malloc (sizeof (x11_visual_t));
  if (vis) {
    vis->display = x->display;
    vis->screen = screen;
    vis->d = x->window;
    vis->dest_size_cb = dest_size_cb;
    vis->frame_output_cb = frame_output_cb;
    vis->user_data = NULL;
  }

  return vis;
}

static init_status_t
xine_player_init (player_t *player)
{
  xine_player_t *x = NULL;
  int verbosity = XINE_VERBOSITY_NONE;

  char *id_vo = NULL;
  char *id_ao = NULL;
  int visual = XINE_VISUAL_TYPE_NONE;
  void *data = NULL;

#ifdef HAVE_DEBUG
  verbosity = XINE_VERBOSITY_LOG;
#endif /* HAVE_DEBUG */

  plog (MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  x = (xine_player_t *) player->priv;

  x->xine = xine_new ();
  xine_init (x->xine);
  xine_engine_set_param (x->xine, XINE_ENGINE_PARAM_VERBOSITY, verbosity);

  switch (player->vo) {
  case PLAYER_VO_NULL:
    id_vo = NULL;
    visual = XINE_VISUAL_TYPE_NONE;
    data = NULL;
    break;

  case PLAYER_VO_X11:
    id_vo = strdup ("x11");
    visual = XINE_VISUAL_TYPE_X11;
    data = x11_init (player);
    break;

  case PLAYER_VO_X11_SDL:
    id_vo = strdup ("sdl");
    visual = XINE_VISUAL_TYPE_X11;
    data = x11_init (player);
    break;

  case PLAYER_VO_XV:
    id_vo = strdup ("xv");
    visual = XINE_VISUAL_TYPE_X11;
    data = x11_init (player);
    break;

  /* TODO, not implemented */
  case PLAYER_VO_FB:
    id_vo = strdup ("fbdev");
    visual = XINE_VISUAL_TYPE_FB;
    data = NULL;
  }

  /* init video output driver */
  if (!(x->vo_port = xine_open_video_driver (x->xine, id_vo, visual, data))) {
    plog (MODULE_NAME, "Xine can't init '%s' video driver", id_vo);
    return PLAYER_INIT_ERROR;
  }

  switch (player->ao) {
  case PLAYER_AO_NULL:
    id_ao = NULL;
    break;

  case PLAYER_AO_ALSA:
    id_ao = strdup ("alsa");
    break;

  case PLAYER_AO_OSS:
    id_ao = strdup ("oss");
  }

  /* init audio output driver */
  if (!(x->ao_port = xine_open_audio_driver (x->xine, id_ao, NULL))) {
    plog (MODULE_NAME, "Xine can't init '%s' audio driver", id_ao);
    return PLAYER_INIT_ERROR;
  }

  if (id_vo)
    free (id_vo);
  if (id_ao);
    free (id_ao);

  x->stream = xine_stream_new (x->xine, x->ao_port, x->vo_port);

  x->event_queue = xine_event_new_queue (x->stream);
  xine_event_create_listener_thread (x->event_queue,
                                     xine_player_event_listener_cb, player);

  /* X11 */
  if (x->display) {
    xine_gui_send_vo_data(x->stream,
                          XINE_GUI_SEND_DRAWABLE_CHANGED, (void *) x->window);
    xine_gui_send_vo_data(x->stream,
                          XINE_GUI_SEND_VIDEOWIN_VISIBLE, (void *) 1);
  }

  return PLAYER_INIT_OK;
}

static void
xine_player_uninit (void *priv)
{
  xine_player_t *x = NULL;

  plog (MODULE_NAME, "uninit");

  if (!priv)
    return;

  x = (xine_player_t *) priv;

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

  /* X11 */
  if (x->display) {
    XLockDisplay (x->display);
    XUnmapWindow (x->display,  x->window);
    XDestroyWindow (x->display, x->window);
    XUnlockDisplay (x->display);
    XCloseDisplay (x->display);
  }

  free (x);
}

static void
xine_player_mrl_get_audio_properties (player_t *player,
                                      mrl_properties_audio_t *audio)
{
  xine_player_t *x = NULL;

  if (!player || !audio)
    return;

  x = (xine_player_t *) player->priv;
  if (!x->stream)
    return;

  if (xine_get_meta_info (x->stream, XINE_META_INFO_AUDIOCODEC))
    audio->codec =
      strdup (xine_get_meta_info (x->stream, XINE_META_INFO_AUDIOCODEC));
  if (audio->codec)
    plog (MODULE_NAME, "Audio Codec: %s", audio->codec);

  audio->bitrate =
    xine_get_stream_info (x->stream, XINE_STREAM_INFO_AUDIO_BITRATE);
  if (audio->bitrate)
    plog (MODULE_NAME, "Audio Bitrate: %d kbps",
        (int) (audio->bitrate / 1000));

  audio->bits =
    xine_get_stream_info (x->stream, XINE_STREAM_INFO_AUDIO_BITS);
  if (audio->bits)
    plog (MODULE_NAME, "Audio Bits: %d bps", audio->bits);

  audio->channels =
    xine_get_stream_info (x->stream, XINE_STREAM_INFO_AUDIO_CHANNELS);
  if (audio->channels)
    plog (MODULE_NAME, "Audio Channels: %d", audio->channels);

  audio->samplerate =
    xine_get_stream_info (x->stream, XINE_STREAM_INFO_AUDIO_SAMPLERATE);
  if (audio->samplerate)
    plog (MODULE_NAME, "Audio Sample Rate: %d Hz", audio->samplerate);
}

static void
xine_player_mrl_get_video_properties (player_t *player,
                                      mrl_properties_video_t *video)
{
  xine_player_t *x = NULL;

  if (!player || !video)
    return;

  x = (xine_player_t *) player->priv;
  if (!x->stream)
    return;

  if (xine_get_meta_info (x->stream, XINE_META_INFO_VIDEOCODEC))
    video->codec =
      strdup (xine_get_meta_info (x->stream, XINE_META_INFO_VIDEOCODEC));
  if (video->codec)
    plog (MODULE_NAME, "Video Codec: %s", video->codec);

  video->bitrate =
    xine_get_stream_info (x->stream, XINE_STREAM_INFO_VIDEO_BITRATE);
  if (video->bitrate)
    plog (MODULE_NAME, "Video Bitrate: %d kbps",
        (int) (video->bitrate / 1000));

  video->width =
    xine_get_stream_info (x->stream, XINE_STREAM_INFO_VIDEO_WIDTH);
  if (video->width)
    plog (MODULE_NAME, "Video Width: %d", video->width);

  video->height =
    xine_get_stream_info (x->stream, XINE_STREAM_INFO_VIDEO_HEIGHT);
  if (video->height)
    plog (MODULE_NAME, "Video Height: %d", video->height);

  video->channels =
    xine_get_stream_info (x->stream, XINE_STREAM_INFO_VIDEO_CHANNELS);
  if (video->channels)
    plog (MODULE_NAME, "Video Channels: %d", video->channels);

  video->streams =
    xine_get_stream_info (x->stream, XINE_STREAM_INFO_VIDEO_STREAMS);
  if (video->streams)
    plog (MODULE_NAME, "Video Streams: %d", video->streams);
}

static void
xine_player_mrl_get_properties (player_t *player)
{
  xine_player_t *x = NULL;
  struct stat st;
  mrl_t *mrl;

  plog (MODULE_NAME, "mrl_get_properties");

  mrl = player->mrl;

  if (!player || !mrl || !mrl->prop)
    return;

  x = (xine_player_t *) player->priv;
  if (!x->stream)
    return;

  /* now fetch properties */
  stat (mrl->name, &st);
  mrl->prop->size = st.st_size;
  plog (MODULE_NAME, "File Size: %.2f MB",
        (float) mrl->prop->size / 1024 / 1024);

  mrl->prop->seekable =
    xine_get_stream_info (x->stream, XINE_STREAM_INFO_SEEKABLE);
  plog (MODULE_NAME, "Seekable: %d", mrl->prop->seekable);

  if (xine_get_stream_info (x->stream, XINE_STREAM_INFO_HAS_AUDIO))
  {
    mrl->prop->audio = mrl_properties_audio_new ();
    xine_player_mrl_get_audio_properties (player, mrl->prop->audio);
  }

  if (xine_get_stream_info (x->stream, XINE_STREAM_INFO_HAS_VIDEO))
  {
    mrl->prop->video = mrl_properties_video_new ();
    xine_player_mrl_get_video_properties (player, mrl->prop->video);
  }
}

static void
xine_player_mrl_get_metadata (player_t *player)
{
  xine_player_t *x = NULL;
  mrl_t *mrl;

  plog (MODULE_NAME, "mrl_get_metadata");

  mrl = player->mrl;

  if (!player || !mrl || !mrl->meta)
    return;

  x = (xine_player_t *) player->priv; 
  if (!x->stream)
    return;

  /* now fetch metadata */
  if (xine_get_meta_info (x->stream, XINE_META_INFO_ARTIST))
    mrl->meta->title =
      strdup (xine_get_meta_info (x->stream, XINE_META_INFO_ARTIST));
  if (mrl->meta->title)
    plog (MODULE_NAME, "Meta Title: %s", mrl->meta->title);

  if (xine_get_meta_info (x->stream, XINE_META_INFO_ARTIST))
    mrl->meta->artist =
      strdup (xine_get_meta_info (x->stream, XINE_META_INFO_ARTIST));
  if (mrl->meta->artist)
    plog (MODULE_NAME, "Meta Artist: %s", mrl->meta->artist);

  if (xine_get_meta_info (x->stream, XINE_META_INFO_GENRE))
    mrl->meta->genre =
      strdup (xine_get_meta_info (x->stream, XINE_META_INFO_GENRE));
  if (mrl->meta->genre)
    plog (MODULE_NAME, "Meta Genre: %s", mrl->meta->genre);

  if (xine_get_meta_info (x->stream, XINE_META_INFO_ALBUM))
    mrl->meta->album =
      strdup (xine_get_meta_info (x->stream, XINE_META_INFO_ALBUM));
  if (mrl->meta->album)
    plog (MODULE_NAME, "Meta Album: %s", mrl->meta->album);

  if (xine_get_meta_info (x->stream, XINE_META_INFO_YEAR))
    mrl->meta->year =
      strdup (xine_get_meta_info (x->stream, XINE_META_INFO_YEAR));
  if (mrl->meta->year)
    plog (MODULE_NAME, "Meta Year: %s", mrl->meta->year);

  if (xine_get_meta_info (x->stream, XINE_META_INFO_TRACK_NUMBER))
    mrl->meta->track =
      strdup (xine_get_meta_info (x->stream, XINE_META_INFO_TRACK_NUMBER));
  if (mrl->meta->track)
    plog (MODULE_NAME, "Meta Track: %s", mrl->meta->track);
}

static playback_status_t
xine_player_playback_start (player_t *player)
{
  xine_player_t *x = NULL;

  plog (MODULE_NAME, "playback_start");

  if (!player)
    return PLAYER_PB_FATAL;

  x = (xine_player_t *) player->priv;

  if (!x->stream)
    return PLAYER_PB_ERROR;

  /* X11 */
  if (x->display && player->mrl->type == PLAYER_MRL_TYPE_VIDEO)
    x11_map (player);

  xine_open (x->stream, player->mrl->name);
  xine_play (x->stream, 0, 0);

  return PLAYER_PB_OK;
}

static void
xine_player_playback_stop (player_t *player)
{
  xine_player_t *x = NULL;

  plog (MODULE_NAME, "playback_stop");

  if (!player)
    return;

  x = (xine_player_t *) player->priv;

  if (!x->stream)
    return;

  /* X11 */
  if (x->display && player->mrl->type == PLAYER_MRL_TYPE_VIDEO)
    x11_unmap (player);

  xine_stop (x->stream);
  xine_close (x->stream);
}

static playback_status_t
xine_player_playback_pause (player_t *player)
{
  xine_player_t *x = NULL;

  plog (MODULE_NAME, "playback_pause");

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
xine_player_playback_seek (player_t *player, int value)
{
  xine_player_t *x = NULL;
  int pos_time = 0, length = 0;

  plog (MODULE_NAME, "playback_seek: %d", value);

  if (!player)
    return;

  x = (xine_player_t *) player->priv;

  if (!x->stream)
    return;

  xine_get_pos_length (x->stream, NULL, &pos_time, &length);
  pos_time += value * 1000;

  if (pos_time < 0)
    pos_time = 0;
  if (pos_time > length)
    pos_time = length;

  xine_play (x->stream, 0, pos_time);
}

static int
xine_player_get_volume (player_t *player)
{
  xine_player_t *x = NULL;

  plog (MODULE_NAME, "get_volume");

  if (!player)
    return -1;

  x = (xine_player_t *) player->priv;

  if (!x->stream)
    return -1;

  return xine_get_param (x->stream, XINE_PARAM_AUDIO_VOLUME);
}

static player_mute_t
xine_player_get_mute (player_t *player)
{
  xine_player_t *x = NULL;

  plog (MODULE_NAME, "get_mute");

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
xine_player_set_volume (player_t *player, int value)
{
  xine_player_t *x = NULL;

  plog (MODULE_NAME, "set_volume: %d", value);

  if (!player)
    return;

  x = (xine_player_t *) player->priv;

  if (!x->stream)
    return;

  xine_set_param (x->stream, XINE_PARAM_AUDIO_VOLUME, value);
}

static void
xine_player_set_mute (player_t *player, player_mute_t value)
{
  xine_player_t *x = NULL;
  int mute = 0;

  if (value == PLAYER_MUTE_UNKNOWN)
    return;

  if (value == PLAYER_MUTE_ON)
    mute = 1;

  plog (MODULE_NAME, "set_mute: %s", mute ? "on" : "off");

  if (!player)
    return;

  x = (xine_player_t *) player->priv;

  if (!x->stream)
    return;

  xine_set_param (x->stream, XINE_PARAM_AUDIO_MUTE, mute);
}

/* public API */
player_funcs_t *
register_functions_xine (void)
{
  player_funcs_t *funcs = NULL;

  funcs = (player_funcs_t *) malloc (sizeof (player_funcs_t));
  funcs->init = xine_player_init;
  funcs->uninit = xine_player_uninit;
  funcs->mrl_get_props = xine_player_mrl_get_properties;
  funcs->mrl_get_meta = xine_player_mrl_get_metadata;
  funcs->pb_start = xine_player_playback_start;
  funcs->pb_stop = xine_player_playback_stop;
  funcs->pb_pause = xine_player_playback_pause;
  funcs->pb_seek = xine_player_playback_seek;
  funcs->get_volume = xine_player_get_volume;
  funcs->get_mute = xine_player_get_mute;
  funcs->set_volume = xine_player_set_volume;
  funcs->set_mute = xine_player_set_mute;

  return funcs;
}

void *
register_private_xine (void)
{
  xine_player_t *x = NULL;

  x = (xine_player_t *) malloc (sizeof (xine_player_t));
  x->xine = NULL;
  x->stream = NULL;
  x->event_queue = NULL;
  x->vo_port = PLAYER_VO_NULL;
  x->ao_port = PLAYER_AO_NULL;
  /* X11 */
  x->display = NULL;

  return x;
}
