/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006-2008 Benjamin Zores <ben@geexbox.org>
 *                         Mathieu Schroeter <mathieu.schroeter@gamesover.ch>
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
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

#include <xine.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "wrapper_xine.h"
#include "x11_common.h"

#define MODULE_NAME "xine"

/* player specific structure */
typedef struct xine_player_s {
  xine_t *xine;
  xine_stream_t *stream;
  xine_event_queue_t *event_queue;
  xine_video_port_t *vo_port;
  xine_audio_port_t *ao_port;
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
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Playback of stream has ended"); 
    if (player->event_cb)
      player->event_cb (PLAYER_EVENT_PLAYBACK_FINISHED, NULL);
    /* X11 */
    if (player->x11 && !mrl_uses_vo (player->mrl))
      x11_unmap (player);
    break;
  }
  case XINE_EVENT_PROGRESS:
  {
    xine_progress_data_t *pevent = (xine_progress_data_t *) event->data;
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "%s [%d%%]", pevent->description, pevent->percent);
    break;
  }
  }
}

static void
send_event (player_t *player, int event)
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
  xine_event.data = NULL;
  xine_event.data_length = 0;

  xine_event_send (x->stream, &xine_event);
}

static void
xine_identify_metadata (mrl_t *mrl, xine_stream_t *stream)
{
  const char *s;
  mrl_metadata_t *meta;

  if (!mrl || !mrl->meta || !stream)
    return;

  meta = mrl->meta;

  s = xine_get_meta_info (stream, XINE_META_INFO_TITLE);
  if (s) {
    if (meta->title)
      free (meta->title);
    meta->title = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_ARTIST);
  if (s) {
    if (meta->artist)
      free (meta->artist);
    meta->artist = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_GENRE);
  if (s) {
    if (meta->genre)
      free (meta->genre);
    meta->genre = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_ALBUM);
  if (s) {
    if (meta->album)
      free (meta->album);
    meta->album = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_YEAR);
  if (s) {
    if (meta->year)
      free (meta->year);
    meta->year = strdup (s);
  }

  s = xine_get_meta_info (stream, XINE_META_INFO_TRACK_NUMBER);
  if (s) {
    if (meta->track)
      free (meta->track);
    meta->track = strdup (s);
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
  if (s) {
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
  if (s) {
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

  video->framerate =
    xine_get_stream_info (stream, XINE_STREAM_INFO_FRAME_DURATION);
}

static void
xine_identify_properties (mrl_t *mrl, xine_stream_t *stream)
{
  if (!mrl || !mrl->prop || !stream)
    return;

  mrl->prop->seekable =
    xine_get_stream_info (stream, XINE_STREAM_INFO_SEEKABLE);
}

static void
xine_identify (player_t *player, mrl_t *mrl, int flags)
{
  xine_player_t *x;
  xine_stream_t *stream;
  xine_video_port_t *vo;
  xine_audio_port_t *ao;

  if (!player || !mrl || !mrl->name)
    return;

  x = (xine_player_t *) player->priv;

  if (!x)
    return;

  ao = xine_open_audio_driver (x->xine, "none", NULL);

  if (!ao)
    return;

  vo = xine_open_video_driver (x->xine, "none", XINE_VISUAL_TYPE_NONE, NULL);

  if (!vo) {
    xine_close_audio_driver (x->xine, ao);
    return;
  }

  stream = xine_stream_new (x->xine, ao, vo);

  if (stream) {
    xine_open (stream, mrl->name);

    if ((flags & IDENTIFY_VIDEO))
      xine_identify_video (mrl, stream);

    if ((flags & IDENTIFY_AUDIO))
      xine_identify_audio (mrl, stream);

    if (flags & IDENTIFY_METADATA)
      xine_identify_metadata (mrl, stream);

    if (flags & IDENTIFY_PROPERTIES)
      xine_identify_properties (mrl, stream);

    xine_close (stream);
    xine_dispose (stream);
  }

  xine_close_audio_driver (x->xine, ao);
  xine_close_video_driver (x->xine, vo);
}

static init_status_t
xine_player_init (player_t *player)
{
  xine_player_t *x = NULL;

  char *id_vo = NULL;
  char *id_ao = NULL;
  int use_x11 = 0;
  int visual = XINE_VISUAL_TYPE_NONE;
  void *data = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  x = (xine_player_t *) player->priv;

  if (!x)
    return PLAYER_INIT_ERROR;

  x->xine = xine_new ();
  xine_init (x->xine);
  xine_engine_set_param (x->xine,
                         XINE_ENGINE_PARAM_VERBOSITY, XINE_VERBOSITY_LOG);

  switch (player->vo) {
  case PLAYER_VO_NULL:
    id_vo = strdup ("none");
    break;

  case PLAYER_VO_X11:
    use_x11 = 1;
    id_vo = strdup ("x11");
    visual = XINE_VISUAL_TYPE_X11;
    break;

  case PLAYER_VO_X11_SDL:
    use_x11 = 1;
    id_vo = strdup ("sdl");
    visual = XINE_VISUAL_TYPE_X11;
    break;

  case PLAYER_VO_XV:
    use_x11 = 1;
    id_vo = strdup ("xv");
    visual = XINE_VISUAL_TYPE_X11;
    break;

  case PLAYER_VO_FB:
    id_vo = strdup ("fb");
    visual = XINE_VISUAL_TYPE_FB;
    break;
    
  default:
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "Unsupported video output type");
    break;
  }

  if (use_x11 && (!x11_init (player) || !x11_get_data (player->x11))) {
    if (id_vo)
      free (id_vo);

    return PLAYER_INIT_ERROR;
  }
  else if (use_x11)
    data = x11_get_data (player->x11);

  /* init video output driver */
  if (!(x->vo_port = xine_open_video_driver (x->xine, id_vo, visual, data))) {
    plog (player, PLAYER_MSG_ERROR,
          MODULE_NAME, "Xine can't init '%s' video driver",
          id_vo ? id_vo : "null");

    if (id_vo)
      free (id_vo);

    return PLAYER_INIT_ERROR;
  }

  if (id_vo)
    free (id_vo);

  switch (player->ao) {
  case PLAYER_AO_NULL:
    id_ao = strdup ("none");
    break;

  case PLAYER_AO_ALSA:
    id_ao = strdup ("alsa");
    break;

  case PLAYER_AO_OSS:
    id_ao = strdup ("oss");
    break;

  default:
    plog (player, PLAYER_MSG_WARNING,
          MODULE_NAME, "Unsupported audio output type");
    break;
  }

  /* init audio output driver */
  if (!(x->ao_port = xine_open_audio_driver (x->xine, id_ao, NULL))) {
    plog (player, PLAYER_MSG_ERROR,
          MODULE_NAME, "Xine can't init '%s' audio driver",
          id_ao ? id_ao : "null");

    if (id_ao);
      free (id_ao);

    return PLAYER_INIT_ERROR;
  }

  if (id_ao);
    free (id_ao);

  x->stream = xine_stream_new (x->xine, x->ao_port, x->vo_port);

  x->event_queue = xine_event_new_queue (x->stream);
  xine_event_create_listener_thread (x->event_queue,
                                     xine_player_event_listener_cb, player);

  /* X11 */
  if (player->x11 && x11_get_display (player->x11)) {
    xine_gui_send_vo_data (x->stream, XINE_GUI_SEND_DRAWABLE_CHANGED,
                           (void *) x11_get_window (player->x11));
    xine_gui_send_vo_data (x->stream, XINE_GUI_SEND_VIDEOWIN_VISIBLE,
                           (void *) 1);
  }

  return PLAYER_INIT_OK;
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
xine_player_uninit (player_t *player)
{
  xine_player_t *x = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "uninit");

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

  /* X11 */
  if (player->x11)
    x11_uninit (player);

  free (x);
}

static void
xine_player_mrl_retrieve_properties (player_t *player, mrl_t *mrl)
{
  mrl_properties_video_t *video;
  mrl_properties_audio_t *audio;
  struct stat st;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "mrl_retrieve_properties");

  if (!player || !mrl || !mrl->prop || !mrl->name)
    return;

  /* now fetch properties */
  stat (mrl->name, &st);
  mrl->prop->size = st.st_size;
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "File Size: %.2f MB",
        (float) mrl->prop->size / 1024 / 1024);

  xine_identify (player, mrl,
                 IDENTIFY_AUDIO | IDENTIFY_VIDEO | IDENTIFY_PROPERTIES);

  audio = mrl->prop->audio;
  video = mrl->prop->video;

  plog (player, PLAYER_MSG_INFO,
        MODULE_NAME, "Seekable: %d", mrl->prop->seekable);

  if (video) {
    if (video->codec)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Video Codec: %s", video->codec);

    if (video->bitrate)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Video Bitrate: %i kbps", video->bitrate / 1000);

    if (video->width)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Video Width: %i", video->width);

    if (video->height)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Video Height: %i", video->height);

    if (video->aspect)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Video Aspect: %.2f", video->aspect);

    if (video->channels)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Video Channels: %i", video->channels);

    if (video->streams)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Video Streams: %i", video->streams);

    if (video->framerate)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Video Framerate: %i", video->framerate);
  }

  if (audio) {
    if (audio->codec)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Audio Codec: %s", audio->codec);

    if (audio->bitrate)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Audio Bitrate: %i kbps", audio->bitrate / 1000);

    if (audio->bits)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Audio Bits: %i bps", audio->bits);

    if (audio->channels)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Audio Channels: %i", audio->channels);

    if (audio->samplerate)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Audio Sample Rate: %i Hz", audio->samplerate);
  }
}

static void
xine_player_mrl_retrieve_metadata (player_t *player, mrl_t *mrl)
{
  mrl_metadata_t *meta;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "mrl_retrieve_metadata");

  if (!player || !mrl || !mrl->meta)
    return;

  xine_identify (player, mrl, IDENTIFY_METADATA);

  meta = mrl->meta;

  if (meta->title)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Title: %s", meta->title);

  if (meta->artist)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Artist: %s", meta->artist);

  if (meta->genre)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Genre: %s", meta->genre);

  if (meta->album)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Album: %s", meta->album);

  if (meta->year)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Year: %s", meta->year);

  if (meta->track)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Track: %s", meta->track);
}

static playback_status_t
xine_player_playback_start (player_t *player)
{
  char *mrl = NULL;
  xine_player_t *x = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_start");

  if (!player)
    return PLAYER_PB_FATAL;

  x = (xine_player_t *) player->priv;

  if (!x->stream)
    return PLAYER_PB_ERROR;

  /* add subtitle to the MRL */
  if (player->mrl->subtitle) {
    mrl = malloc (strlen (player->mrl->name) +
                  strlen (player->mrl->subtitle) + 11);

    if (mrl)
      sprintf (mrl, "%s#subtitle:%s",
               player->mrl->name, player->mrl->subtitle);
  }
  /* or take only the name */
  else
    mrl = strdup (player->mrl->name);

  if (!mrl)
    return PLAYER_PB_ERROR;

  /* X11 */
  if (player->x11 && !mrl_uses_vo (player->mrl))
    x11_map (player);

  xine_open (x->stream, mrl);
  xine_play (x->stream, 0, 0);

  free (mrl);

  return PLAYER_PB_OK;
}

static void
xine_player_playback_stop (player_t *player)
{
  xine_player_t *x = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_stop");

  if (!player)
    return;

  x = (xine_player_t *) player->priv;

  if (!x->stream)
    return;

  /* X11 */
  if (player->x11 && !mrl_uses_vo (player->mrl))
    x11_unmap (player);

  xine_stop (x->stream);
  xine_close (x->stream);
}

static playback_status_t
xine_player_playback_pause (player_t *player)
{
  xine_player_t *x = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_pause");

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

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_seek: %d", value);

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

static void
xine_player_playback_dvdnav (player_t *player, player_dvdnav_t value)
{
  char log[8];
  int event;

  switch (value)
  {
  case PLAYER_DVDNAV_UP:
    strcpy (log, "up");
    event = XINE_EVENT_INPUT_UP;
    break;

  case PLAYER_DVDNAV_DOWN:
    strcpy (log, "down");
    event = XINE_EVENT_INPUT_DOWN;
    break;

  case PLAYER_DVDNAV_LEFT:
    strcpy (log, "left");
    event = XINE_EVENT_INPUT_LEFT;
    break;

  case PLAYER_DVDNAV_RIGHT:
    strcpy (log, "right");
    event = XINE_EVENT_INPUT_RIGHT;
    break;

  case PLAYER_DVDNAV_MENU:
    strcpy (log, "menu");
    /* go to root menu if possible */
    event = XINE_EVENT_INPUT_MENU3;
    break;

  case PLAYER_DVDNAV_SELECT:
    strcpy (log, "select");
    event = XINE_EVENT_INPUT_SELECT;
    break;

  default:
    return;
  }

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_dvdnav: %s", log);

  if (!player)
    return;

  if (player->mrl->resource == PLAYER_MRL_RESOURCE_DVDNAV ||
      player->mrl->resource == PLAYER_MRL_RESOURCE_DVD)
    send_event (player, event);
}

static int
xine_player_get_volume (player_t *player)
{
  xine_player_t *x = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "get_volume");

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

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "get_mute");

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

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "set_volume: %d", value);

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

  plog (player, PLAYER_MSG_INFO,
        MODULE_NAME, "set_mute: %s", mute ? "on" : "off");

  if (!player)
    return;

  x = (xine_player_t *) player->priv;

  if (!x->stream)
    return;

  xine_set_param (x->stream, XINE_PARAM_AUDIO_MUTE, mute);
}

static void
xine_player_set_sub_delay (player_t *player, float value)
{
  int delay;
  xine_player_t *x = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "set_sub_delay: %.2f", value);

  /* unit is 1/90000 sec */
  delay = (int) rintf (value * 90000.0);

  if (!player)
    return;

  x = (xine_player_t *) player->priv;

  if (!x->stream)
    return;

  xine_set_param (x->stream, XINE_PARAM_SPU_OFFSET, delay);
}

/* public API */
player_funcs_t *
register_functions_xine (void)
{
  player_funcs_t *funcs = NULL;

  funcs = calloc (1, sizeof (player_funcs_t));
  funcs->init               = xine_player_init;
  funcs->uninit             = xine_player_uninit;
  funcs->set_verbosity      = xine_player_set_verbosity;
  funcs->mrl_retrieve_props = xine_player_mrl_retrieve_properties;
  funcs->mrl_retrieve_meta  = xine_player_mrl_retrieve_metadata;
  funcs->pb_start           = xine_player_playback_start;
  funcs->pb_stop            = xine_player_playback_stop;
  funcs->pb_pause           = xine_player_playback_pause;
  funcs->pb_seek            = xine_player_playback_seek;
  funcs->pb_dvdnav          = xine_player_playback_dvdnav;
  funcs->get_volume         = xine_player_get_volume;
  funcs->get_mute           = xine_player_get_mute;
  funcs->get_time_pos       = NULL;
  funcs->set_volume         = xine_player_set_volume;
  funcs->set_mute           = xine_player_set_mute;
  funcs->set_sub_delay      = xine_player_set_sub_delay;

  return funcs;
}

void *
register_private_xine (void)
{
  xine_player_t *x = NULL;

  x = calloc (1, sizeof (xine_player_t));
  x->xine = NULL;
  x->stream = NULL;
  x->event_queue = NULL;
  x->vo_port = PLAYER_VO_NULL;
  x->ao_port = PLAYER_AO_NULL;

  return x;
}
