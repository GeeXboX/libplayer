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

#ifndef FIFO_QUEUE_H
#define FIFO_QUEUE_H

typedef struct fifo_queue_s fifo_queue_t;

enum fifo_queue_errno {
  FIFO_QUEUE_ERROR_QUEUE  = -3,
  FIFO_QUEUE_ERROR_EMPTY  = -2,
  FIFO_QUEUE_ERROR_MALLOC = -1,
  FIFO_QUEUE_SUCCESS      =  0,
};


fifo_queue_t *pl_fifo_queue_new (void);
void pl_fifo_queue_free (fifo_queue_t *queue);

int pl_fifo_queue_push (fifo_queue_t *queue, int id, void *data);
int pl_fifo_queue_pop (fifo_queue_t *queue, int *id, void **data);

#endif /* FIFO_QUEUE_H */
