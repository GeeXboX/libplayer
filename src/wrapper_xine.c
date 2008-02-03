/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006-2007 Benjamin Zores <ben@geexbox.org>
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
    plog (MODULE_NAME, "Playback of stream has ended"); 
    if (player->event_cb)
      player->event_cb (PLAYER_EVENT_PLAYBACK_FINISHED, NULL);
    /* X11 */
    if (player->x11 && mrl_uses_vo (player->mrl))
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

static init_status_t
xine_player_init (player_t *player)
{
  xine_player_t *x = NULL;
  int verbosity = XINE_VERBOSITY_NONE;

  char *id_vo = NULL;
  char *id_ao = NULL;
  int use_x11 = 0;
  int visual = XINE_VISUAL_TYPE_NONE;
  void *data = NULL;

#ifdef HAVE_DEBUG
  verbosity = XINE_VERBOSITY_LOG;
#endif /* HAVE_DEBUG */

  plog (MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  x = (xine_player_t *) player->priv;

  if (!x)
    return PLAYER_INIT_ERROR;

  x->xine = xine_new ();
  xine_init (x->xine);
  xine_engine_set_param (x->xine, XINE_ENGINE_PARAM_VERBOSITY, verbosity);

  switch (player->vo) {
  case PLAYER_VO_NULL:
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
    plog (MODULE_NAME, "Unsupported video output type\n");
    break;
  }

  if (use_x11 && (!x11_init (player) || !player->x11->data)) {
    if (id_vo)
      free (id_vo);

    return PLAYER_INIT_ERROR;
  }
  else if (use_x11)
    data = player->x11->data;

  /* init video output driver */
  if (!(x->vo_port = xine_open_video_driver (x->xine, id_vo, visual, data))) {
    plog (MODULE_NAME, "Xine can't init '%s' video driver",
          id_vo ? id_vo : "null");

    if (id_vo)
      free (id_vo);

    return PLAYER_INIT_ERROR;
  }

  if (id_vo)
    free (id_vo);

  switch (player->ao) {
  case PLAYER_AO_NULL:
    break;

  case PLAYER_AO_ALSA:
    id_ao = strdup ("alsa");
    break;

  case PLAYER_AO_OSS:
    id_ao = strdup ("oss");
  }

  /* init audio output driver */
  if (!(x->ao_port = xine_open_audio_driver (x->xine, id_ao, NULL))) {
    plog (MODULE_NAME, "Xine can't init '%s' audio driver",
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
  if (player->x11 && player->x11->display) {
    xine_gui_send_vo_data (x->stream, XINE_GUI_SEND_DRAWABLE_CHANGED,
                           (void *) player->x11->window);
    xine_gui_send_vo_data (x->stream, XINE_GUI_SEND_VIDEOWIN_VISIBLE,
                           (void *) 1);
  }

  return PLAYER_INIT_OK;
}

static void
xine_player_uninit (player_t *player)
{
  xine_player_t *x = NULL;

  plog (MODULE_NAME, "uninit");

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
  char *mrl = NULL;
  xine_player_t *x = NULL;

  plog (MODULE_NAME, "playback_start");

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
  if (player->x11 && mrl_uses_vo (player->mrl))
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

  plog (MODULE_NAME, "playback_stop");

  if (!player)
    return;

  x = (xine_player_t *) player->priv;

  if (!x->stream)
    return;

  /* X11 */
  if (player->x11 && mrl_uses_vo (player->mrl))
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

  plog (MODULE_NAME, "playback_dvdnav: %s", log);

  if (!player)
    return;

  if (player->mrl->type == PLAYER_MRL_TYPE_DVD_NAV)
    send_event (player, event);
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

static void
xine_player_set_sub_delay (player_t *player, float value)
{
  int delay;
  xine_player_t *x = NULL;

  plog (MODULE_NAME, "set_sub_delay: %.2f", value);

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
  funcs->init            = xine_player_init;
  funcs->uninit          = xine_player_uninit;
  funcs->set_verbosity   = NULL;
  funcs->mrl_get_props   = xine_player_mrl_get_properties;
  funcs->mrl_get_meta    = xine_player_mrl_get_metadata;
  funcs->pb_start        = xine_player_playback_start;
  funcs->pb_stop         = xine_player_playback_stop;
  funcs->pb_pause        = xine_player_playback_pause;
  funcs->pb_seek         = xine_player_playback_seek;
  funcs->pb_dvdnav       = xine_player_playback_dvdnav;
  funcs->get_volume      = xine_player_get_volume;
  funcs->get_mute        = xine_player_get_mute;
  funcs->set_volume      = xine_player_set_volume;
  funcs->set_mute        = xine_player_set_mute;
  funcs->set_sub_delay   = xine_player_set_sub_delay;

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
