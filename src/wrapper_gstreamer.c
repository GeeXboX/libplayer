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

#include <gst/gst.h>
#include <gst/interfaces/streamvolume.h>

#ifdef HAVE_WIN_XCB
#include <gst/interfaces/xoverlay.h>
#endif /* HAVE_WIN_XCB */

#include "player.h"
#include "player_internals.h"
#include "playlist.h"
#include "logs.h"
#include "event.h"
#include "fs_utils.h"
#include "window.h"
#include "wrapper_gstreamer.h"

#define MODULE_NAME "gstreamer"

#define NS_TO_MS(ns) (ns / 1000000)
#define MS_TO_NS(ms) (ms * 1000000)

/* player specific structure */
typedef struct gstreamer_player_s {
  GstBus *bus;
  GstElement *bin;
  GstElement *video_sink;
  GstElement *audio_sink;
  GstElement *volume_ctrl;
} gstreamer_player_t;

typedef struct gstreamer_identifier_s {
  player_t *player;
  mrl_t *mrl;
  char *audio_codec;
  char *video_codec;
  GstElement *bin;
  int flags;
} gstreamer_identifier_t;


static char *
get_uri (mrl_t *m)
{
  switch (m->resource)
  {
  case MRL_RESOURCE_FILE:
  {
    mrl_resource_local_args_t *args;

    args = m->priv;
    if (!args)
      break;

    /* check if given MRL is a relative UNIX path */
    if (args->location[0] == '/')
    {
      char uri[1024];
      snprintf (uri, sizeof (uri), "file://%s", args->location);
      return strdup (uri);
    }

    return strdup (args->location);
  }
  default:
    break;
  }
  return NULL;
}

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

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "Message Type: %s", GST_MESSAGE_TYPE_NAME (msg));

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

    pl_log (player, PLAYER_MSG_ERROR, MODULE_NAME, "%s", err->message);
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
    pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME,
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
#ifdef HAVE_WIN_XCB
  int ret;
#endif /* HAVE_WIN_XCB */

  if (!player)
    return NULL;

  switch (player->vo)
  {
  case PLAYER_VO_AUTO:
    sink = gst_element_factory_make ("gconfvideosink", VIDEO_SINK_NAME);
    if (!sink)
      sink = gst_element_factory_make ("autovideosink", VIDEO_SINK_NAME);
    break;
  case PLAYER_VO_NULL:
    sink = gst_element_factory_make ("fakesink", VIDEO_SINK_NAME);
    if (sink)
      g_object_set (sink, "sync", TRUE, NULL);
    break;
  case PLAYER_VO_X11:
    sink = gst_element_factory_make ("ximagesink", VIDEO_SINK_NAME);
    break;
  case PLAYER_VO_X11_SDL:
    sink = gst_element_factory_make ("sdlvideosink", VIDEO_SINK_NAME);
    break;
  case PLAYER_VO_XV:
    sink = gst_element_factory_make ("xvimagesink", VIDEO_SINK_NAME);
    break;
  case PLAYER_VO_GL:
    sink = gst_element_factory_make ("glimagesink", VIDEO_SINK_NAME);
    break;
  case PLAYER_VO_V4L2:
    sink = gst_element_factory_make ("v4l2sink", VIDEO_SINK_NAME);
    break;
  default:
    break;
  }

#ifdef HAVE_WIN_XCB
  ret = pl_window_init (player->window);
  if (player->vo != PLAYER_VO_AUTO && !ret)
  {
    gst_object_unref (GST_OBJECT (sink));
    pl_log (player, PLAYER_MSG_ERROR,
            MODULE_NAME, "X initialization has failed");
    return NULL;
  }
#endif /* HAVE_WIN_XCB */

  return sink;
}

#define AUDIO_SINK_NAME "audio-sink"

static GstElement *
gstreamer_set_audio_sink (player_t *player)
{
  GstElement *sink = NULL;

  if (!player)
    return NULL;

  switch (player->ao)
  {
  case PLAYER_AO_AUTO:
    sink = gst_element_factory_make ("gconfaudiosink", AUDIO_SINK_NAME);
    if (!sink)
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

#ifdef HAVE_WIN_XCB
static GstBusSyncReply
bus_sync_handler_cb (pl_unused GstBus *bus, GstMessage *message, gpointer data)
{
  player_t *player = data;
  const GstStructure *str;
  GstXOverlay *ov;

  str = gst_message_get_structure (message);
  if (!str)
    return GST_BUS_PASS;

  if (!gst_structure_has_name (str, "prepare-xwindow-id"))
    return GST_BUS_PASS;

  ov = GST_X_OVERLAY (GST_MESSAGE_SRC (message));
  if (ov)
    gst_x_overlay_set_xwindow_id (ov, pl_window_winid_get (player->window));

  return GST_BUS_DROP;
}
#endif /* HAVE_WIN_XCB */

static void
gstreamer_get_tag (GstTagList *list, char **meta, const gchar *tag)
{
  gboolean r;

  if (!list || !tag)
    return;

  switch (gst_tag_get_type (tag))
  {
  case G_TYPE_STRING:
  {
    gchar *value;

    r = gst_tag_list_get_string (list, tag, &value);
    if (!r)
      return;

    PFREE (*meta);
    *meta = strdup (value);

    g_free (value);
    break;
  }
  case G_TYPE_UINT:
  {
    guint value;
    char buf[128] = { 0 };

    r = gst_tag_list_get_uint (list, tag, &value);
    if (!r)
      return;

    snprintf (buf, sizeof (buf), "%d", value);
    PFREE (*meta);
    *meta = strdup (buf);
    break;
  }
  case G_TYPE_INT:
  {
    gint value;
    char buf[128] = { 0 };

    r = gst_tag_list_get_int (list, tag, &value);
    if (!r)
      return;

    snprintf (buf, sizeof (buf), "%d", value);
    PFREE (*meta);
    *meta = strdup (buf);
    break;
  }
  }
}

#define GET_TAG(tag, name) \
  gstreamer_get_tag (tags, &meta->tag, GST_TAG_##name);

static void
identify_get_props (gstreamer_identifier_t *id)
{
  GstElement *bin = id->bin;
  mrl_properties_t *prop = id->mrl->prop;
  gint n_video, n_audio, i;

  g_object_get (bin,
                "n-audio", &n_audio,
                "n-video", &n_video,
                NULL);

  pl_log (id->player, PLAYER_MSG_VERBOSE, MODULE_NAME,
          "n_audio=%d, n_video=%d", n_audio, n_video);

  /*
   * note that caps chould change dynamically.. but I don't think libplayer
   * has any mechanism to deal with dynamically changing caps, so I'll
   * ignore installing notify::caps signal handlers on the pads..
   */

  if (n_video > 0)
  {
    GstCaps *caps = NULL;

    if (!prop->video)
      prop->video = mrl_properties_video_new ();

    for (i = 0; i < n_video && !caps; i++)
    {
      GstPad *pad = NULL;
      g_signal_emit_by_name (bin, "get-video-pad", i, &pad);

      pl_log (id->player, PLAYER_MSG_VERBOSE, MODULE_NAME,
              "video: pad=%"GST_PTR_FORMAT, pad);

      if (pad)
      {
        caps = gst_pad_get_negotiated_caps (pad);
        gst_object_unref (pad);
      }
    }

    if (caps)
    {
      GstStructure *s = gst_caps_get_structure (caps, 0);
      gint fps_n, fps_d;

      pl_log (id->player, PLAYER_MSG_VERBOSE, MODULE_NAME,
          "video: caps=%"GST_PTR_FORMAT, caps);

      if (id->video_codec)
        prop->video->codec = strdup (id->video_codec);

      gst_structure_get_int (s, "width",  (gint *) &prop->video->width);
      gst_structure_get_int (s, "height", (gint *) &prop->video->height);

      /* XXX prop->video->aspect */

      if (gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d))
      {
        prop->video->frameduration = (uint32_t)
          (PLAYER_VIDEO_FRAMEDURATION_RATIO_DIV * (float) fps_d / fps_n);
      }

      gst_caps_unref (caps);
    }
  }

  if (n_audio > 0)
  {
    GstCaps *caps = NULL;

    if (!prop->audio)
      prop->audio = mrl_properties_audio_new ();

    for (i = 0; i < n_audio && !caps; i++)
    {
      GstPad *pad = NULL;
      g_signal_emit_by_name (bin, "get-audio-pad", i, &pad);

      pl_log (id->player, PLAYER_MSG_VERBOSE, MODULE_NAME,
              "audio: pad=%"GST_PTR_FORMAT, pad);

      if (pad)
      {
        caps = gst_pad_get_negotiated_caps (pad);
        gst_object_unref(pad);
      }
    }

    if (caps)
    {
      GstStructure *s = gst_caps_get_structure (caps, 0);

      pl_log (id->player, PLAYER_MSG_VERBOSE, MODULE_NAME,
              "audio: caps=%"GST_PTR_FORMAT, caps);

      /* XXX prop->audio->bitrate comes from tags?? */

      if (id->audio_codec)
        prop->audio->codec = strdup (id->audio_codec);

      gst_structure_get_int (s, "channels", (gint *) &prop->audio->channels);
      gst_structure_get_int (s, "rate",     (gint *) &prop->audio->samplerate);
      gst_structure_get_int (s, "width",    (gint *) &prop->audio->bits);

      gst_caps_unref (caps);
    }
  }
}

static gboolean
identify_bus_callback (pl_unused GstBus *bus, GstMessage *msg, gpointer data)
{
  gstreamer_identifier_t *id = data;

  pl_log (id->player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "Message Type: %s", GST_MESSAGE_TYPE_NAME (msg));

  switch (GST_MESSAGE_TYPE (msg))
  {
  case GST_MESSAGE_ERROR:
  {
    GError *err = NULL;
    gchar *dbg_info = NULL;

    gst_message_parse_error (msg, &err, &dbg_info);
    pl_log (id->player, PLAYER_MSG_ERROR, MODULE_NAME,
            "error from element: %s: %s",
            GST_OBJECT_NAME (msg->src), err->message);
    pl_log (id->player, PLAYER_MSG_VERBOSE, MODULE_NAME,
            "debugging info: %s", dbg_info ? dbg_info : "none");
    g_error_free (err);
    g_free (dbg_info);
    /* fall-thru */
  }
  case GST_MESSAGE_ASYNC_DONE:
  {
    if (id->mrl->prop)
    {
      GstFormat fmt = GST_FORMAT_TIME;
      gint64 len;

      if (gst_element_query_duration (id->bin, &fmt, &len)
          && (fmt == GST_FORMAT_TIME))
      {
        id->mrl->prop->length = NS_TO_MS (len);

        /*
         * TODO maybe we should attempt a seek to start or end of file to
         * figure out if the seek completes.. but the seek operation
         * completes asynchronously so for now we just assume that if we
         * can figure out the duration, then we can probably seek too..
         */
        id->mrl->prop->seekable = TRUE;

        pl_log (id->player, PLAYER_MSG_VERBOSE, MODULE_NAME,
                "len=%"PRIi64", assuming seekable..", len);
      }

      identify_get_props (id);
    }

    /* we now have enough stream information, shut it down */
    gst_element_set_state (id->bin, GST_STATE_NULL);
    return FALSE;
  }
  case GST_MESSAGE_TAG:
  {
    GstTagList *tags = NULL;
    mrl_metadata_t *meta = id->mrl->meta;

    gst_message_parse_tag (msg, &tags);

    /* the video/audio codec is needed for props */
    gstreamer_get_tag (tags, &id->video_codec, GST_TAG_VIDEO_CODEC);
    gstreamer_get_tag (tags, &id->audio_codec, GST_TAG_AUDIO_CODEC);

    /* only fetch metadata if really requested */
    if (id->flags & IDENTIFY_METADATA)
    {
      GET_TAG (title,   TITLE);
      GET_TAG (artist,  ARTIST);
      GET_TAG (album,   ALBUM);
      GET_TAG (genre,   GENRE);
      GET_TAG (comment, COMMENT);
      GET_TAG (track,   TRACK_NUMBER);
    }

    gst_tag_list_free (tags);
    break;
  }
  default:
    break;
  }
  return TRUE;
}

static void
gstreamer_identify (player_t *player, mrl_t *mrl, int flags)
{
  GstBus *bus;
  GstElement *bin, *vs, *as;
  gstreamer_identifier_t *id;
  char *uri;

  /*
   * TODO can we avoid constructing a pipeline twice and simply get all props
   * and metadata in one shot?
   */

  if (mrl->prop)
  {
    if (mrl->resource == MRL_RESOURCE_FILE)
    {
      mrl_resource_local_args_t *args = mrl->priv;
      if (args && args->location)
      {
        const char *location = args->location;

        if (strstr (location, "file://") == location)
          location += 7;

        mrl->prop->size = pl_file_size (location);
      }
    }
  }

  /* create a new pipeline for stream identification */
  bin = gst_element_factory_make ("playbin2", "identifier");
  bus = gst_pipeline_get_bus (GST_PIPELINE (bin));

  /* create a fake video sink */
  vs = gst_element_factory_make ("fakesink", VIDEO_SINK_NAME);
  g_object_set (vs, "sync", TRUE, NULL);
  g_object_set (G_OBJECT (bin), "video-sink", vs, NULL);

  /* create a fake audio sink */
  as = gst_element_factory_make ("fakesink", AUDIO_SINK_NAME);
  g_object_set (G_OBJECT (bin), "audio-sink", as, NULL);

  /* map the identification struct */
  id              = malloc (sizeof (gstreamer_identifier_t));
  id->player      = player;
  id->mrl         = mrl;
  id->bin         = bin;
  id->flags       = flags;
  id->audio_codec = NULL;
  id->video_codec = NULL;

  uri = get_uri (mrl);
  if (uri)
  {
    pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "identify: %s", uri);

    g_object_set (bin, "uri", uri, NULL);

    /*
     * Put GStreamer engine in paused mode
     * Everything will be cleaned up by event loop at message reception
     */
    gst_element_set_state (bin, GST_STATE_PAUSED);

    /* wait for stream parsing event */
    while (1)
    {
      GstMessage *msg =
        gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
                                    GST_MESSAGE_ASYNC_DONE |
                                    GST_MESSAGE_TAG | GST_MESSAGE_ERROR);

      if (!identify_bus_callback (bus, msg, id))
        break;
    }
  }
  else
    pl_log (player, PLAYER_MSG_WARNING, MODULE_NAME,
            "unrecognized resource type: %d", mrl->resource);

  PFREE (uri);
  PFREE (id->audio_codec);
  PFREE (id->video_codec);
}

#define GST_SIGNAL(msg, cb) \
  g_signal_connect (g->bin, msg,  G_CALLBACK (cb), player)

static init_status_t
gstreamer_player_init (player_t *player)
{
  gstreamer_player_t *g = NULL;
  GError *error;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "init");
  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME,
          "Library Version is: %s", gst_version_string ());

  if (!player)
    return PLAYER_INIT_ERROR;

  g = player->priv;

  if (!g)
    return PLAYER_INIT_ERROR;

  g_thread_init (NULL);

  if (!gst_init_check (NULL, NULL, &error))
    return PLAYER_INIT_ERROR;

  g->bin = gst_element_factory_make ("playbin2", "player");

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

  /*
   * If we're using an audio sink that has a volume property,
   * then that's what we need to modify for volume control,
   * not the playbin's one
   */
  g->volume_ctrl =
    g_object_class_find_property (G_OBJECT_GET_CLASS (g->audio_sink),
                                  "volume") ? g->audio_sink : g->bin;

  gst_element_set_state (g->bin, GST_STATE_NULL);

#ifdef HAVE_WIN_XCB
  gst_bus_set_sync_handler (g->bus, bus_sync_handler_cb, player);
#endif /* HAVE_WIN_XCB */

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

  pl_window_uninit (player->window);

  gst_object_unref (GST_OBJECT (g->bin));
  gst_object_unref (GST_OBJECT (g->bus));

  gst_deinit ();

  PFREE (g);
}

static void
gstreamer_set_verbosity (player_t *player, player_verbosity_level_t level)
{
  gstreamer_player_t *g;
  GstDebugLevel verbosity = GST_LEVEL_DEFAULT;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "set_verbosity");

  if (!player)
    return;

  g = (gstreamer_player_t *) player->priv;
  if (!g)
    return;

  switch (level)
  {
  case PLAYER_MSG_NONE:
    verbosity = GST_LEVEL_NONE;
    break;

  case PLAYER_MSG_VERBOSE:
    verbosity = GST_LEVEL_DEBUG;
    break;

  case PLAYER_MSG_INFO:
    verbosity = GST_LEVEL_INFO;
    break;

  case PLAYER_MSG_WARNING:
    verbosity = GST_LEVEL_WARNING;
    break;

  case PLAYER_MSG_ERROR:
    verbosity = GST_LEVEL_ERROR;
    break;

  case PLAYER_MSG_CRITICAL:
    verbosity = GST_LEVEL_FIXME;
    break;
  }

#if 0
  gst_debug_set_default_threshold (verbosity);
  gst_debug_set_active (1);
#endif
}

static void
gstreamer_mrl_retrieve_properties (player_t *player, mrl_t *mrl)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player || !mrl || !mrl->prop)
    return;

  gstreamer_identify (player, mrl,
                      IDENTIFY_AUDIO | IDENTIFY_VIDEO | IDENTIFY_PROPERTIES);
}

static void
gstreamer_mrl_retrieve_metadata (player_t *player, mrl_t *mrl)
{
  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, __FUNCTION__);

  if (!player || !mrl || !mrl->meta)
    return;

  gstreamer_identify (player, mrl, IDENTIFY_METADATA);
}

static int
gstreamer_get_time_pos (player_t *player)
{
  gstreamer_player_t *g;
  GstFormat fmt = GST_FORMAT_TIME;
  gint64 pos;
  int res;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "get_time_pos");

  if (!player)
    return -1;

  g = (gstreamer_player_t *) player->priv;
  if (!g || !g->bin)
    return -1;

  res = gst_element_query_position (g->bin, &fmt, &pos);

  return res ? NS_TO_MS (pos) : -1;
}

static int
gstreamer_get_percent_pos (player_t *player)
{
  gstreamer_player_t *g;
  GstFormat fmt = GST_FORMAT_TIME;
  gint64 pos, len;
  int res;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "get_percent_pos");

  if (!player)
    return -1;

  g = (gstreamer_player_t *) player->priv;
  if (!g || !g->bin)
    return -1;

  res = gst_element_query_position (g->bin, &fmt, &pos);
  if (!res)
    return -1;

  res = gst_element_query_duration (g->bin, &fmt, &len);
  if (!res)
    return -1;

  return (int) (pos * 100 / len);
}

static playback_status_t
gstreamer_player_playback_start (player_t *player)
{
  char *uri = NULL;
  gstreamer_player_t *g;
  mrl_t *mrl;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_start");

  if (!player)
    return PLAYER_PB_FATAL;

  mrl = pl_playlist_get_mrl (player->playlist);
  if (!mrl)
    return PLAYER_PB_ERROR;

  g = player->priv;

  uri = get_uri (mrl);
  if (uri)
  {
    g_object_set (G_OBJECT (g->bin), "uri", uri, NULL);

    /* put GStreamer engine in playback state */
    gst_element_set_state (g->bin, GST_STATE_PLAYING);
  }
  else
  {
    pl_log (player, PLAYER_MSG_WARNING, MODULE_NAME,
            "unrecognized resource type: %d", mrl->resource);
  }

  PFREE (uri);

  if (MRL_USES_VO (mrl))
    pl_window_map (player->window);

  return PLAYER_PB_OK;
}

static void
gstreamer_player_playback_stop (player_t *player)
{
  gstreamer_player_t *g = NULL;
  mrl_t *mrl;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_stop");

  if (!player)
    return;

  g = player->priv;

  gst_element_set_state (g->bin, GST_STATE_NULL);

  mrl = pl_playlist_get_mrl (player->playlist);
  if (MRL_USES_VO (mrl))
    pl_window_unmap (player->window);
}

static playback_status_t
gstreamer_player_playback_pause (player_t *player)
{
  gstreamer_player_t *g;
  GstStateChangeReturn st;
  GstState state;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "playback_pause");

  if (!player)
    return PLAYER_PB_FATAL;

  g = (gstreamer_player_t *) player->priv;
  if (!g || !g->bin)
    return PLAYER_PB_FATAL;

  /* check current playback status */
  st = gst_element_get_state (g->bin, &state, NULL, GST_CLOCK_TIME_NONE);
  if (st == GST_STATE_CHANGE_SUCCESS)
  {
    if (state == GST_STATE_PAUSED)
      gst_element_set_state (g->bin, GST_STATE_PLAYING);
    else
      gst_element_set_state (g->bin, GST_STATE_PAUSED);

    return PLAYER_PB_OK;
  }

  return PLAYER_PB_ERROR;
}

static void
gstreamer_player_playback_seek (player_t *player,
                                int value, player_pb_seek_t seek)
{
  gstreamer_player_t *g;
  GstFormat fmt = GST_FORMAT_TIME;
  gint64 pos, len;
  int res;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "playback_seek: %d %d", value, seek);

  if (!player)
    return;

  g = (gstreamer_player_t *) player->priv;
  if (!g || !g->bin)
    return;

  res = gst_element_query_position (g->bin, &fmt, &pos);
  if (!res)
    return;

  res = gst_element_query_duration (g->bin, &fmt, &len);
  if (!res)
    return;

  switch (seek)
  {
  default:
  case PLAYER_PB_SEEK_RELATIVE:
    pos += MS_TO_NS (value);

    if (pos < 0)
      pos = 0;
    if (pos > len)
      pos = len;

    break;
  case PLAYER_PB_SEEK_PERCENT:
    pos = value * len / 100;
    break;
  case PLAYER_PB_SEEK_ABSOLUTE:
    pos = MS_TO_NS (value);
    break;
  }

  res = gst_element_seek (g->bin, 1.0,
                          GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
                          GST_SEEK_TYPE_SET, pos,
                          GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

  if (!res)
    pl_log (player, PLAYER_MSG_WARNING, MODULE_NAME, "playback_seek failed");
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

  return !(player->ao == PLAYER_AO_NULL);
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
    volume = value / 100.0;

    gst_element_get_state (es, &cur_state, NULL, GST_CLOCK_TIME_NONE);
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

static player_mute_t
gstreamer_audio_get_mute (player_t *player)
{
  player_mute_t mute = PLAYER_MUTE_UNKNOWN;
  gstreamer_player_t *g;
  GstElement *es;

  pl_log (player, PLAYER_MSG_VERBOSE, MODULE_NAME, "audio_get_mute");

  if (!player)
    return mute;

  g  = (gstreamer_player_t *) player->priv;
  es = g->volume_ctrl;

  mute = gst_stream_volume_get_mute (GST_STREAM_VOLUME (es)) ?
    PLAYER_MUTE_ON : PLAYER_MUTE_OFF;

  return mute;
}

static void
gstreamer_audio_set_mute (player_t *player, player_mute_t value)
{
  gstreamer_player_t *g;
  gboolean mute = FALSE;
  GstElement *es;

  if (value == PLAYER_MUTE_UNKNOWN)
    return;

  if (value == PLAYER_MUTE_ON)
    mute = TRUE;

  pl_log (player, PLAYER_MSG_VERBOSE,
          MODULE_NAME, "audio_set_mute: %s", mute ? "on" : "off");

  if (!player)
    return;

  g  = (gstreamer_player_t *) player->priv;
  es = g->volume_ctrl;

  gst_stream_volume_set_mute (GST_STREAM_VOLUME (es), mute);
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

  funcs = PCALLOC (player_funcs_t, 1);
  if (!funcs)
    return NULL;

  funcs->init               = gstreamer_player_init;
  funcs->uninit             = gstreamer_player_uninit;
  funcs->set_verbosity      = gstreamer_set_verbosity;

  funcs->mrl_retrieve_props = gstreamer_mrl_retrieve_properties;
  funcs->mrl_retrieve_meta  = gstreamer_mrl_retrieve_metadata;
  funcs->mrl_video_snapshot = NULL;

  funcs->get_time_pos       = gstreamer_get_time_pos;
  funcs->get_percent_pos    = gstreamer_get_percent_pos;
  funcs->set_framedrop      = NULL;
  funcs->set_mouse_pos      = NULL;
  funcs->osd_show_text      = NULL;
  funcs->osd_state          = NULL;

  funcs->pb_start           = gstreamer_player_playback_start;
  funcs->pb_stop            = gstreamer_player_playback_stop;
  funcs->pb_pause           = gstreamer_player_playback_pause;
  funcs->pb_seek            = gstreamer_player_playback_seek;
  funcs->pb_seek_chapter    = NULL;
  funcs->pb_set_speed       = NULL;

  funcs->audio_get_volume   = gstreamer_audio_get_volume;
  funcs->audio_set_volume   = gstreamer_audio_set_volume;
  funcs->audio_get_mute     = gstreamer_audio_get_mute;
  funcs->audio_set_mute     = gstreamer_audio_set_mute;
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

  funcs->radio_channel_set  = PL_NOT_SUPPORTED;
  funcs->radio_channel_prev = PL_NOT_SUPPORTED;
  funcs->radio_channel_next = PL_NOT_SUPPORTED;

  funcs->vdr                = PL_NOT_SUPPORTED;

  return funcs;
}

void *
pl_register_private_gstreamer (void)
{
  gstreamer_player_t *g = NULL;

  g = PCALLOC (gstreamer_player_t, 1);
  if (!g)
    return NULL;

  return g;
}
