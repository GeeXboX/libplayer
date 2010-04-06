/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2008 Benjamin Zores <ben@geexbox.org>
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

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>

#include <gst/gst.h>
#include <gst/interfaces/streamvolume.h>

#ifdef USE_X11
#include <gst/interfaces/xoverlay.h>
#endif /* USE_X11 */

#include "player.h"
#include "player_internals.h"
#include "playlist.h"
#include "logs.h"
#include "event.h"
#include "wrapper_gstreamer.h"
#ifdef USE_X11
#include "x11_common.h"
#endif /* USE_X11 */

#define MODULE_NAME "gstreamer"

/* player specific structure */
typedef struct gstreamer_player_s {
  GstBus *bus;
  GstElement *bin;
  GstElement *video_sink;
  GstElement *audio_sink;
  GstElement *volume_ctrl;
} gstreamer_player_t;

static void
gstreamer_set_eof (player_t *player)
{
  gstreamer_player_t *g;

  if (!player)
    return;

  g = player->priv;

  /* properly shutdown playback engine */
  gst_element_set_state (g->bin, GST_STATE_NULL);

  /* tell player */
  player_event_send (player, PLAYER_EVENT_PLAYBACK_FINISHED);
}

static gboolean
bus_callback (pl_unused GstBus *bus, GstMessage *msg, gpointer data)
{
  player_t *player      = data;
  gstreamer_player_t *g = player->priv;

  switch (GST_MESSAGE_TYPE (msg))
  {
  case GST_MESSAGE_EOS:
  {
    pl_log (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Playback of stream has ended");

    gstreamer_set_eof (player);
    break;
  }
  case GST_MESSAGE_ERROR:
  {
    gchar *debug;
    GError *err;

    gst_message_parse_error (msg, &err, &debug);
    g_free (debug);

    pl_log (player, PLAYER_MSG_ERROR,
            MODULE_NAME, "Error: %s", err->message);
    g_error_free (err);

    gstreamer_set_eof (player);
    break;
  }
  case GST_MESSAGE_STATE_CHANGED:
  {
    GstState old_state, new_state;
    gchar *src;

    gst_message_parse_state_changed (msg, &old_state, &new_state, NULL);

    if (old_state == new_state)
      break;

    src = gst_object_get_name (msg->src);
    pl_log (player, PLAYER_MSG_VERBOSE,
	    "%s changed state from %s to %s", src,
	    gst_element_state_get_name (old_state),
	    gst_element_state_get_name (new_state));
    g_free (src);
    break;
  }
  default:
    pl_log (player, PLAYER_MSG_VERBOSE,
            MODULE_NAME, "Unhandled message: %" GST_PTR_FORMAT, msg);
    break;
  }

  return TRUE;
}

#define VIDEO_SINK_NAME "video-sink"

static GstElement *
gstreamer_set_video_sink (player_t *player)
{
  GstElement *sink = NULL;
  int use_x11 = 0;

  if (!player)
    return NULL;

  switch (player->vo)
  {
  case PLAYER_VO_AUTO:
    sink = gst_element_factory_make ("autovideosink", VIDEO_SINK_NAME);
    break;
  case PLAYER_VO_NULL:
    sink = gst_element_factory_make ("fakesink", VIDEO_SINK_NAME);
    if (sink)
      g_object_set (sink, "sync", TRUE, NULL);
    break;
  case PLAYER_VO_X11:
    sink = gst_element_factory_make ("ximagesink", VIDEO_SINK_NAME);
    use_x11 = 1;
    break;
  case PLAYER_VO_X11_SDL:
    sink = gst_element_factory_make ("sdlvideosink", VIDEO_SINK_NAME);
    use_x11 = 1;
    break;
  case PLAYER_VO_XV:
    sink = gst_element_factory_make ("xvimagesink", VIDEO_SINK_NAME);
    use_x11 = 1;
    break;
  default:
    break;
  }

#ifdef USE_X11
  if (sink && use_x11)
  {
    GstXOverlay *ov;
    uint32_t winid;
    int ret = 0;

    ret = pl_x11_init (player);
    if (player->vo != PLAYER_VO_AUTO && !ret)
    {
      gst_object_unref (GST_OBJECT (sink));
      pl_log (player, PLAYER_MSG_ERROR,
              MODULE_NAME, "X initialization has failed");
      return NULL;
    }

    winid = pl_x11_get_window (player->x11);

    ov = GST_X_OVERLAY (sink);
    gst_x_overlay_set_xwindow_id (ov, winid);
  }
#endif /* USE_X11 */

  return sink;
}

#define AUDIO_SINK_NAME "audio-sink"

static GstElement *
gstreamer_set_audio_sink (player_t *player)
{
  GstElement *sink = NULL;

  if (!player)
    return NULL;

  switch (player->ao) {
  case PLAYER_AO_AUTO:
    sink = gst_element_factory_make ("autoaudiosink", AUDIO_SINK_NAME);
    break;
  case PLAYER_AO_NULL:
    sink = gst_element_factory_make ("fakesink", AUDIO_SINK_NAME);
    break;
  case PLAYER_AO_ALSA:
    sink = gst_element_factory_make ("alsasink", AUDIO_SINK_NAME);
    break;
  case PLAYER_AO_OSS:
    sink = gst_element_factory_make ("osssink", AUDIO_SINK_NAME);
    break;
  case PLAYER_AO_PULSE:
    sink = gst_element_factory_make ("pulsesink", AUDIO_SINK_NAME);
    break;
  default:
    break;
  }

  return sink;
}

static init_status_t
gstreamer_player_init (player_t *player)
{
  gstreamer_player_t *g = NULL;
  GError *error;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  g = player->priv;

  if (!g)
    return PLAYER_INIT_ERROR;

  if (!gst_init_check (NULL, NULL, &error))
    return PLAYER_INIT_ERROR;

  g->bin = gst_element_factory_make ("playbin", "player");

  g->bus = gst_pipeline_get_bus (GST_PIPELINE (g->bin));
  if (!g->bus)
  {
    gst_element_set_state (g->bin, GST_STATE_NULL);
    gst_object_unref (GST_OBJECT (g->bin));
    gst_deinit ();
    return PLAYER_INIT_ERROR;
  }
  gst_bus_add_watch (g->bus, bus_callback, player);

  /* set video sink */
  g->video_sink = gstreamer_set_video_sink (player);
  if (g->video_sink)
    g_object_set (G_OBJECT (g->bin), "video-sink", g->video_sink, NULL);

  /* set audio sink */
  g->audio_sink = gstreamer_set_audio_sink (player);
  if (g->audio_sink)
    g_object_set (G_OBJECT (g->bin), "audio-sink", g->audio_sink, NULL);

  /* If we're using an audio sink that has a volume property,
     then that's what we need to modify for volume control,
     not the playbin's one */
  g->volume_ctrl =
    g_object_class_find_property (G_OBJECT_GET_CLASS (g->audio_sink),
				  "volume") ? g->audio_sink : g->bin;

  gst_element_set_state (g->bin, GST_STATE_NULL);

  return PLAYER_INIT_OK;
}

static void
gstreamer_player_uninit (player_t *player)
{
  gstreamer_player_t *g = NULL;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "uninit");

  if (!player)
    return;

  g = player->priv;
  if (!g)
    return;

  gst_element_set_state (g->bin, GST_STATE_NULL);

#ifdef USE_X11
  pl_x11_uninit (player);
#endif /* USE_X11 */

  gst_object_unref (GST_OBJECT (g->bin));
  gst_object_unref (GST_OBJECT (g->bus));

  gst_deinit ();

  free (g);
}

static playback_status_t
gstreamer_player_playback_start (player_t *player)
{
  char mrl[PATH_MAX + 16] = { 0 };
  gstreamer_player_t *g;
  mrl_t *m;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_start");

  if (!player)
    return PLAYER_PB_FATAL;

  m = pl_playlist_get_mrl (player->playlist);
  if (!m)
    return PLAYER_PB_ERROR;

  g = player->priv;

  switch (m->resource)
  {
  case MRL_RESOURCE_FILE:
  {
    mrl_resource_local_args_t *args;

    args = m->priv;
    if (!args)
      break;

    /* check if given MRL is a relative path */
    if (args->location[0] != '/')
    {
      char *cwd;
      cwd = get_current_dir_name ();
      sprintf (mrl, "file://%s/%s", cwd, args->location);
      free (cwd);
    }
    else
      sprintf (mrl, "file://%s", args->location);
    break;
  }
  default:
    break;
  }

  g_object_set (G_OBJECT (g->bin), "uri", mrl, NULL);

  /* put GStreamer engine in playback state */
  gst_element_set_state (g->bin, GST_STATE_PLAYING);

#ifdef USE_X11
  //if (MRL_USES_VO (m)) /* properties retrieval is not yet working */
    pl_x11_map (player);
#endif /* USE_X11 */

  return PLAYER_PB_OK;
}

static void
gstreamer_player_playback_stop (player_t *player)
{
  gstreamer_player_t *g = NULL;
#ifdef USE_X11
  //mrl_t *mrl; /* properties retrieval is not yet working */
#endif /* USE_X11 */

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_stop");

  if (!player)
    return;

  g = player->priv;

  gst_element_set_state (g->bin, GST_STATE_NULL);

#ifdef USE_X11
  //mrl = pl_playlist_get_mrl (player->playlist);
  //if (MRL_USES_VO (mrl)) /* properties retrieval is not yet working */
    pl_x11_unmap (player);
#endif /* USE_X11 */
}

static int
gstreamer_audio_get_volume (player_t *player)
{
  gstreamer_player_t *g;
  GstElement *es;
  gdouble vol;
  int volume;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "audio_get_volume");

  if (!player)
    return -1;

  g = player->priv;
  es = g->volume_ctrl;

  if (gst_element_implements_interface (es, GST_TYPE_STREAM_VOLUME))
    vol = gst_stream_volume_get_volume (GST_STREAM_VOLUME (es),
					GST_STREAM_VOLUME_FORMAT_CUBIC);
  else
    g_object_get (G_OBJECT (es), "volume", &vol, NULL);

  volume = (int) (100 * vol);

  return (volume < 0) ? -1 : volume;
}

static gboolean
gstreamer_audio_can_set_volume (player_t *player)
{
  gstreamer_player_t *g;

  g = player->priv;
  g_return_val_if_fail (GST_IS_ELEMENT (g->bin), FALSE);

  /* TODO: return FALSE if using AC3 Passthrough */

  return (player->ao == PLAYER_AO_NULL) ? 0 : 1;
}

static void
gstreamer_audio_set_volume (player_t *player, int value)
{
  gstreamer_player_t *g;
  GstState cur_state;
  GstElement *es;
  double volume;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "audio_set_volume: %d", value);

  if (!player)
    return;

  g = player->priv;
  es = g->volume_ctrl;

  if (gstreamer_audio_can_set_volume (player))
  {
    volume = ((double) (value)) / 100.0;

    gst_element_get_state (es, &cur_state, NULL, 0);
    if (cur_state == GST_STATE_READY || cur_state == GST_STATE_PLAYING)
    {
      if (gst_element_implements_interface (es, GST_TYPE_STREAM_VOLUME))
	gst_stream_volume_set_volume (GST_STREAM_VOLUME (es),
				      GST_STREAM_VOLUME_FORMAT_CUBIC, volume);
      else
	g_object_set (es, "volume", volume, NULL);
    }
  }
}

/*****************************************************************************/
/*                            Public Wrapper API                             */
/*****************************************************************************/

int
pl_supported_resources_gstreamer (mrl_resource_t res)
{
  switch (res)
  {
  case MRL_RESOURCE_FILE:
    return 1;

  default:
    return 0;
  }
}

player_funcs_t *
pl_register_functions_gstreamer (void)
{
  player_funcs_t *funcs = NULL;

  funcs = calloc (1, sizeof (player_funcs_t));
  if (!funcs)
    return NULL;

  funcs->init               = gstreamer_player_init;
  funcs->uninit             = gstreamer_player_uninit;
  funcs->set_verbosity      = NULL;

  funcs->mrl_retrieve_props = NULL;
  funcs->mrl_retrieve_meta  = NULL;
  funcs->mrl_video_snapshot = NULL;

  funcs->get_time_pos       = NULL;
  funcs->get_percent_pos    = NULL;
  funcs->set_framedrop      = NULL;
  funcs->set_mouse_pos      = NULL;
  funcs->osd_show_text      = NULL;
  funcs->osd_state          = NULL;

  funcs->pb_start           = gstreamer_player_playback_start;
  funcs->pb_stop            = gstreamer_player_playback_stop;
  funcs->pb_pause           = NULL;
  funcs->pb_seek            = NULL;
  funcs->pb_seek_chapter    = NULL;
  funcs->pb_set_speed       = NULL;

  funcs->audio_get_volume   = gstreamer_audio_get_volume;
  funcs->audio_set_volume   = gstreamer_audio_set_volume;
  funcs->audio_get_mute     = NULL;
  funcs->audio_set_mute     = NULL;
  funcs->audio_set_delay    = NULL;
  funcs->audio_select       = NULL;
  funcs->audio_prev         = NULL;
  funcs->audio_next         = NULL;

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
pl_register_private_gstreamer (void)
{
  gstreamer_player_t *g = NULL;

  g = calloc (1, sizeof (gstreamer_player_t));
  if (!g)
    return NULL;

  return g;
}
