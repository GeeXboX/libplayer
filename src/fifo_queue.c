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
#include <semaphore.h>
#include <stdlib.h>
#include <stdint.h>

#include "player.h"
#include "player_internals.h"
#include "fifo_queue.h"

typedef struct fifo_queue_item_s {
  int id;
  void *data;
  struct fifo_queue_item_s *next;
} fifo_queue_item_t;

struct fifo_queue_s {
  fifo_queue_item_t *item;
  fifo_queue_item_t *item_last;
  pthread_mutex_t mutex;
  sem_t sem;
};


fifo_queue_t *
pl_fifo_queue_new (void)
{
  fifo_queue_t *queue;

  queue = PCALLOC (fifo_queue_t, 1);
  if (!queue)
    return NULL;

  pthread_mutex_init (&queue->mutex, NULL);
  sem_init (&queue->sem, 0, 0);

  return queue;
}

void
pl_fifo_queue_free (fifo_queue_t *queue)
{
  fifo_queue_item_t *item, *next;

  if (!queue)
    return;

  item = queue->item;
  while (item)
  {
    next = item->next;
    PFREE (item);
    item = next;
  }

  pthread_mutex_destroy (&queue->mutex);
  sem_destroy (&queue->sem);

  PFREE (queue);
}

int
pl_fifo_queue_push (fifo_queue_t *queue, int id, void *data)
{
  fifo_queue_item_t *item;

  if (!queue)
    return FIFO_QUEUE_ERROR_QUEUE;

  pthread_mutex_lock (&queue->mutex);

  item = queue->item;
  if (item)
  {
    queue->item_last->next = PCALLOC (fifo_queue_item_t, 1);
    item = queue->item_last->next;
  }
  else
  {
    item = PCALLOC (fifo_queue_item_t, 1);
    queue->item = item;
  }

  if (!item)
  {
    pthread_mutex_unlock (&queue->mutex);
    return FIFO_QUEUE_ERROR_MALLOC;
  }

  queue->item_last = item;

  item->id = id;
  item->data = data;

  /* new entry in the queue is ok */
  sem_post (&queue->sem);

  pthread_mutex_unlock (&queue->mutex);

  return FIFO_QUEUE_SUCCESS;
}

int
pl_fifo_queue_pop (fifo_queue_t *queue, int *id, void **data)
{
  fifo_queue_item_t *item, *next;

  if (!queue)
    return FIFO_QUEUE_ERROR_QUEUE;

  /* wait on the queue */
  sem_wait (&queue->sem);

  pthread_mutex_lock (&queue->mutex);
  item = queue->item;
  if (!item)
  {
    pthread_mutex_unlock (&queue->mutex);
    return FIFO_QUEUE_ERROR_EMPTY;
  }

  if (id)
    *id = item->id;
  if (data)
    *data = item->data;

  /* remove the entry and go to the next */
  next = item->next;
  PFREE (item);
  queue->item = next;
  pthread_mutex_unlock (&queue->mutex);

  return FIFO_QUEUE_SUCCESS;
}
