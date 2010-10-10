/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2008 Mathieu Schroeter <mathieu@schroetersa.ch>
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

#include <pthread.h>
#include <stdlib.h>

#include "player.h"
#include "player_internals.h"
#include "fifo_queue.h"
#include "event_handler.h"

struct event_handler_s {
  fifo_queue_t *queue;
  pthread_t th_handler;

  int run;
  int enable;
  pthread_mutex_t mutex_run;
  pthread_mutex_t mutex_enable;

  /* to synchronize with a supervisor (for example) */
  int *sync_run;
  pthread_t *sync_job;
  pthread_cond_t *sync_cond;
  pthread_mutex_t *sync_mutex;

  void *data;
  int (*event_cb) (void *data, int e);
};


static void
pl_event_handler_sync_catch (event_handler_t *handler)
{
  if (!handler)
    return;

  if (!handler->sync_run
      || !handler->sync_job || !handler->sync_cond || !handler->sync_mutex)
    return;

  pthread_mutex_lock (handler->sync_mutex);
  /* someone already running? */
  if (*handler->sync_run &&
      !pthread_equal (*handler->sync_job, handler->th_handler))
    pthread_cond_wait (handler->sync_cond, handler->sync_mutex);
  *handler->sync_job = handler->th_handler;
  *handler->sync_run = 1;
  pthread_mutex_unlock (handler->sync_mutex);
}

void
pl_event_handler_sync_release (event_handler_t *handler)
{
  if (!handler)
    return;

  if (!handler->sync_run
      || !handler->sync_job || !handler->sync_cond || !handler->sync_mutex)
    return;

  pthread_mutex_lock (handler->sync_mutex);
  *handler->sync_run = 0;
  pthread_cond_signal (handler->sync_cond); /* release for "other" */
  pthread_mutex_unlock (handler->sync_mutex);
}

static void *
thread_handler (void *arg)
{
  int run;
  event_handler_t *handler = arg;

  if (!handler)
    pthread_exit (NULL);

  while (1)
  {
    int e = 0;
    int res;

    res = pl_fifo_queue_pop (handler->queue, &e, NULL);

    /* stay alive? */
    pthread_mutex_lock (&handler->mutex_run);
    run = handler->run;
    pthread_mutex_unlock (&handler->mutex_run);

    if (!run)
      break;

    if (res) /* error? retry */
      continue;

    pl_event_handler_sync_catch (handler);

    handler->event_cb (handler->data, e);
  }

  pthread_exit (NULL);
}

event_handler_t *
pl_event_handler_register (void *data,
                           int (*event_cb) (void *data, int e))
{
  event_handler_t *handler;

  if (!data || !event_cb)
    return NULL;

  handler = PCALLOC (event_handler_t, 1);
  if (!handler)
    return NULL;

  handler->queue = pl_fifo_queue_new ();
  if (!handler->queue)
  {
    PFREE (handler);
    return NULL;
  }

  pthread_mutex_init (&handler->mutex_enable, NULL);
  pthread_mutex_init (&handler->mutex_run, NULL);
  handler->data = data;
  handler->event_cb = event_cb;

  return handler;
}

int
pl_event_handler_init (event_handler_t *handler, int *run, pthread_t *job,
                       pthread_cond_t *cond, pthread_mutex_t *mutex)
{
  int res;
  pthread_attr_t attr;

  if (!handler)
    return EVENT_HANDLER_ERROR_HANDLER;

  handler->run = 1;

  /* use an external sync? */
  if (run && job && cond && mutex)
  {
    handler->sync_run = run;
    handler->sync_job = job;
    handler->sync_cond = cond;
    handler->sync_mutex = mutex;
  }

  pthread_attr_init (&attr);
  pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_JOINABLE);

  res = pthread_create (&handler->th_handler, &attr, thread_handler, handler);
  if (res)
  {
    handler->run = 0;
    res = EVENT_HANDLER_ERROR_THREAD;
  }
  else
    res = EVENT_HANDLER_SUCCESS;

  pthread_attr_destroy (&attr);
  return res;
}

void
pl_event_handler_uninit (event_handler_t *handler)
{
  void *ret;

  if (!handler)
    return;

  pthread_mutex_lock (&handler->mutex_run);
  handler->run = 0;
  pthread_mutex_unlock (&handler->mutex_run);

  pl_event_handler_enable (handler);
  pl_event_handler_send (handler, 0);
  pthread_join (handler->th_handler, &ret);

  if (handler->queue)
    pl_fifo_queue_free (handler->queue);

  pthread_mutex_destroy (&handler->mutex_enable);
  pthread_mutex_destroy (&handler->mutex_run);

  PFREE (handler);
}

int
pl_event_handler_enable (event_handler_t *handler)
{
  if (!handler)
    return EVENT_HANDLER_ERROR_HANDLER;

  pthread_mutex_lock (&handler->mutex_enable);
  handler->enable = 1;
  pthread_mutex_unlock (&handler->mutex_enable);

  return EVENT_HANDLER_SUCCESS;
}

int
pl_event_handler_disable (event_handler_t *handler)
{
  if (!handler)
    return EVENT_HANDLER_ERROR_HANDLER;

  pthread_mutex_lock (&handler->mutex_enable);
  handler->enable = 0;
  pthread_mutex_unlock (&handler->mutex_enable);

  return EVENT_HANDLER_SUCCESS;
}

int
pl_event_handler_send (event_handler_t *handler, int e)
{
  int res;
  int enable;

  if (!handler)
    return EVENT_HANDLER_ERROR_HANDLER;

  pthread_mutex_lock (&handler->mutex_enable);
  enable = handler->enable;
  pthread_mutex_unlock (&handler->mutex_enable);

  if (!enable)
    return EVENT_HANDLER_ERROR_DISABLE;

  res = pl_fifo_queue_push (handler->queue, e, NULL);
  if (res)
    return EVENT_HANDLER_ERROR_SEND;

  return EVENT_HANDLER_SUCCESS;
}

pthread_t
pl_event_handler_tid (event_handler_t *handler)
{
  return handler->th_handler;
}
