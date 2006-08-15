/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006 Benjamin Zores <ben@geexbox.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include <stdlib.h>
#include <xine.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "wrapper_xine.h"

#define MODULE_NAME "xine"

/* player specific structure */
struct xine_player_t {
  xine_t *xine;
  xine_stream_t *stream;
  xine_event_queue_t *event_queue;
  xine_video_port_t *vo_port;
  xine_audio_port_t *ao_port;
};

static void
xine_player_event_listener_cb (void *user_data, const xine_event_t *event)
{
  struct player_t *player = NULL;

  player = (struct player_t *) user_data;
  if (!player)
    return;

  switch (event->type)
  {
  case XINE_EVENT_UI_PLAYBACK_FINISHED:
  {
    plog (MODULE_NAME, "Playback of stream has ended"); 
    if (player->event_cb)
      player->event_cb (PLAYER_EVENT_PLAYBACK_FINISHED, NULL);
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
static init_status_t
xine_player_init (struct player_t *player)
{
  struct xine_player_t *x = NULL;
  
  plog (MODULE_NAME, "init");

  if (!player)
    return PLAYER_INIT_ERROR;

  x = (struct xine_player_t *) player->priv;

  x->xine = xine_new ();
  xine_init (x->xine);
  xine_engine_set_param (x->xine, XINE_ENGINE_PARAM_VERBOSITY,
                         XINE_VERBOSITY_NONE);

  /* init video output driver: for now, just set to NULL */
  x->vo_port = xine_open_video_driver (x->xine, NULL,
                                       XINE_VISUAL_TYPE_NONE, NULL);

  /* init audio output driver */
  x->ao_port = xine_open_audio_driver (x->xine,
                                       player->ao ? player->ao : NULL, NULL);

  x->stream = xine_stream_new (x->xine, x->ao_port, x->vo_port);
  
  x->event_queue = xine_event_new_queue (x->stream);
  xine_event_create_listener_thread (x->event_queue,
                                     xine_player_event_listener_cb, player);


  return PLAYER_INIT_OK;
}

static void
xine_player_uninit (void *priv)
{
  struct xine_player_t *x = NULL;
  
  plog (MODULE_NAME, "uninit");

  if (!priv)
    return;

  x = (struct xine_player_t *) priv;

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

  free (x);
}

static playback_status_t
xine_player_playback_start (struct player_t *player)
{
  struct xine_player_t *x = NULL;

  plog (MODULE_NAME, "playback_start");
  
  if (!player)
    return PLAYER_PB_FATAL;

  x = (struct xine_player_t *) player->priv;
  
  if (!x->stream)
    return PLAYER_PB_ERROR;
  
  xine_open (x->stream, player->mrl->name);
  xine_play (x->stream, 0, 0);

  return PLAYER_PB_OK;
}

static void
xine_player_playback_stop (struct player_t *player)
{
  struct xine_player_t *x = NULL;

  plog (MODULE_NAME, "playback_stop");

  if (!player)
    return;

  x = (struct xine_player_t *) player->priv;

  if (!x->stream)
    return;
  
  xine_stop (x->stream);
  xine_close (x->stream);
}

static playback_status_t
xine_player_playback_pause (struct player_t *player)
{
  struct xine_player_t *x = NULL;

  plog (MODULE_NAME, "playback_pause");
  
  if (!player)
    return PLAYER_PB_FATAL;

  x = (struct xine_player_t *) player->priv;

  if (!x->stream)
    return PLAYER_PB_ERROR;

  if (xine_get_param (x->stream, XINE_PARAM_SPEED) != XINE_SPEED_PAUSE)
    xine_set_param (x->stream, XINE_PARAM_SPEED, XINE_SPEED_PAUSE);
  else
    xine_set_param (x->stream, XINE_PARAM_SPEED, XINE_SPEED_NORMAL);

  return PLAYER_PB_OK;
}

static void
xine_player_playback_seek (struct player_t *player, int value)
{
  struct xine_player_t *x = NULL;
  int pos_time = 0, length = 0;
  
  plog (MODULE_NAME, "playback_seek: %d", value);
  
  if (!player)
    return;

  x = (struct xine_player_t *) player->priv;

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
xine_player_get_volume (struct player_t *player)
{
  struct xine_player_t *x = NULL;

  plog (MODULE_NAME, "get_volume");
  
  if (!player)
    return -1;

  x = (struct xine_player_t *) player->priv;

  if (!x->stream)
    return -1;

  return xine_get_param (x->stream, XINE_PARAM_AUDIO_VOLUME);
}

static void
xine_player_set_volume (struct player_t *player, int value)
{
  struct xine_player_t *x = NULL;

  plog (MODULE_NAME, "set_volume: %d", value);

  if (!player)
    return;

  x = (struct xine_player_t *) player->priv;

  if (!x->stream)
    return;

  xine_set_param (x->stream, XINE_PARAM_AUDIO_VOLUME, value);
}

/* public API */
struct player_funcs_t *
register_functions_xine (void)
{
  struct player_funcs_t *funcs = NULL;

  funcs = (struct player_funcs_t *) malloc (sizeof (struct player_funcs_t));
  funcs->init = xine_player_init;
  funcs->uninit = xine_player_uninit;
  funcs->pb_start = xine_player_playback_start;
  funcs->pb_stop = xine_player_playback_stop;
  funcs->pb_pause = xine_player_playback_pause;
  funcs->pb_seek = xine_player_playback_seek;
  funcs->get_volume = xine_player_get_volume;
  funcs->set_volume = xine_player_set_volume;
  
  return funcs;
}

void *
register_private_xine (void)
{
  struct xine_player_t *x = NULL;

  x = (struct xine_player_t *) malloc (sizeof (struct xine_player_t));
  x->xine = NULL;
  x->stream = NULL;
  x->event_queue = NULL;
  x->vo_port = NULL;
  x->ao_port = NULL;

  return x;
}
