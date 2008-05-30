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
 * Foundation, Inc, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "wrapper_gstreamer.h"
#include "x11_common.h"

#define MODULE_NAME "gstreamer"

/* player specific structure */
typedef struct gstreamer_player_s {
  GMainLoop *loop;
  GstBus *bus;
  GstElement *bin;
  GstElement *video_sink;
  GstElement *audio_sink;
} gstreamer_player_t;

static gboolean
bus_callback (GstBus *bus, GstMessage *msg, gpointer data)
{
  player_t *player = (player_t *) data;
  gstreamer_player_t *g = (gstreamer_player_t *) player->priv;
  GMainLoop *loop = (GMainLoop *) g->loop;

  switch (GST_MESSAGE_TYPE (msg))
  {
  case GST_MESSAGE_EOS:
  {
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Playback of stream has ended"); 

    /* properly shutdown playback engine */
    g_main_loop_quit (loop);
    gst_element_set_state (g->bin, GST_STATE_NULL);
    
    /* tell player */
    if (player->event_cb)
      player->event_cb (PLAYER_EVENT_PLAYBACK_FINISHED, NULL);
    
    break;
  }
  case GST_MESSAGE_ERROR:
  {
    gchar *debug;
    GError *err;
    
    gst_message_parse_error (msg, &err, &debug);
    g_free (debug);

    plog (player, PLAYER_MSG_ERROR,
          MODULE_NAME, "Error: %s", err->message);
    g_error_free (err);

    /* properly shutdown playback engine */
    g_main_loop_quit (loop);
    gst_element_set_state (g->bin, GST_STATE_NULL); 
    break;
  }
  default:
    break;
  }

  return TRUE;
}

static init_status_t
gstreamer_player_init (player_t *player)
{
  gstreamer_player_t *g = NULL;
  GError *error;
  
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  g = (gstreamer_player_t *) player->priv;

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

  switch (player->vo)
  {
  case PLAYER_VO_X11:
    g->video_sink = gst_element_factory_make ("ximagesink", "x11-output");
    break;
  case PLAYER_VO_X11_SDL:
    g->video_sink = gst_element_factory_make ("sdlvideosink", "sdl-output");
    break;
  case PLAYER_VO_XV:
    if (!g->video_sink)
      g->video_sink = gst_element_factory_make ("xvimagesink", "xv-output");
    break;
  default:
  break;
  }

  if (g->video_sink)
    g_object_set (G_OBJECT (g->bin), "video-sink", g->video_sink, NULL);

  switch (player->ao) {
  case PLAYER_AO_ALSA:
    g->audio_sink = gst_element_factory_make ("alsasink", "alsa-output");
    break;
  case PLAYER_AO_OSS:
    g->audio_sink = gst_element_factory_make ("osssink", "oss-output");
    break;
  default:
    break;
  }

  if (g->audio_sink)
    g_object_set (G_OBJECT (g->bin), "audio-sink", g->audio_sink, NULL);

  gst_element_set_state (g->bin, GST_STATE_NULL);
  g->loop = g_main_loop_new (NULL, FALSE);
  
  return PLAYER_INIT_OK;
}

static void
gstreamer_player_uninit (player_t *player)
{
  gstreamer_player_t *g = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "uninit");

  if (!player)
    return;

  g = (gstreamer_player_t *) player->priv;

  if (!g)
    return;

  gst_element_set_state (g->bin, GST_STATE_NULL);
  g_main_loop_quit (g->loop);
  g_main_loop_unref (g->loop);
  
  gst_object_unref (GST_OBJECT (g->bin));
  gst_object_unref (GST_OBJECT (g->bus));

  gst_deinit ();

  free (g);
}

static void *
gstreamer_playback_thread (void *arg)
{
  player_t *player = (player_t *) arg;
  gstreamer_player_t *g = (gstreamer_player_t *) player->priv;
  g_main_loop_run (g->loop);
  return NULL;
}

static playback_status_t
gstreamer_player_playback_start (player_t *player)
{
  char mrl[PATH_MAX + 16];
  gstreamer_player_t *g;
  pthread_attr_t attr;
  pthread_t th;
  
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_start");

  if (!player)
    return PLAYER_PB_FATAL;

  g = (gstreamer_player_t *) player->priv;
  memset (mrl, '\0', PATH_MAX + 16);
  
  switch (player->mrl->resource)
  {
  case PLAYER_MRL_RESOURCE_FILE:
  {
    /* check if given MRL is a relative path */
    if (player->mrl->name[0] != '/')
    {
      char *cwd;
      cwd = get_current_dir_name ();
      sprintf (mrl, "file://%s/%s", cwd, player->mrl->name);
      free (cwd);
    }
    else
      sprintf (mrl, "file://%s", player->mrl->name);
    break;
  }
  default:
    break;
  }

  g_object_set (G_OBJECT (g->bin), "uri", mrl, NULL);

  /* put GStreamer engine in playback state */
  gst_element_set_state (g->bin, GST_STATE_PLAYING);

  /* create the playback thread */
  pthread_attr_init (&attr);
  pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_JOINABLE);
  pthread_create (&th, &attr, gstreamer_playback_thread, player);
  pthread_attr_destroy (&attr);
  
  return PLAYER_PB_OK;
}

static void
gstreamer_player_playback_stop (player_t *player)
{
  gstreamer_player_t *g = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "playback_stop");

  if (!player)
    return;

  g = (gstreamer_player_t *) player->priv;

  gst_element_set_state (g->bin, GST_STATE_NULL);
  g_main_loop_quit (g->loop);
}

/* public API */
player_funcs_t *
register_functions_gstreamer (void)
{
  player_funcs_t *funcs = NULL;

  funcs = calloc (1, sizeof (player_funcs_t));
  funcs->init            = gstreamer_player_init;
  funcs->uninit          = gstreamer_player_uninit;
  funcs->set_verbosity   = NULL;
  funcs->mrl_retrieve_props   = NULL;
  funcs->mrl_retrieve_meta    = NULL;
  funcs->pb_start        = gstreamer_player_playback_start;
  funcs->pb_stop         = gstreamer_player_playback_stop;
  funcs->pb_pause        = NULL;
  funcs->pb_seek         = NULL;
  funcs->pb_dvdnav       = NULL;
  funcs->get_volume      = NULL;
  funcs->get_mute        = NULL;
  funcs->get_time_pos    = NULL;
  funcs->set_volume      = NULL;
  funcs->set_mute        = NULL;
  funcs->set_sub_delay   = NULL;
  
  return funcs;
}

void *
register_private_gstreamer (void)
{
  gstreamer_player_t *g = NULL;

  g = calloc (1, sizeof (gstreamer_player_t));
  g->loop = NULL;
  g->bin = NULL;
  g->bus = NULL;
  g->video_sink = NULL;
  g->audio_sink = NULL;

  return g;
}
