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

#ifndef EVENT_HANDLER_H
#define EVENT_HANDLER_H

typedef struct event_handler_s event_handler_t;

enum event_handler_errno {
  EVENT_HANDLER_ERROR_DISABLE = -4,
  EVENT_HANDLER_ERROR_HANDLER = -3,
  EVENT_HANDLER_ERROR_THREAD  = -2,
  EVENT_HANDLER_ERROR_SEND    = -1,
  EVENT_HANDLER_SUCCESS       =  0,
};


event_handler_t *pl_event_handler_register (void *data,
                                            int (*event_cb) (void *data,
                                                             int e));
int pl_event_handler_init (event_handler_t *handler, int *run,
                           pthread_t *job, pthread_cond_t *cond,
                           pthread_mutex_t *mutex);
void pl_event_handler_uninit (event_handler_t *handler);

int pl_event_handler_send (event_handler_t *handler, int e);
int pl_event_handler_enable (event_handler_t *handler);
int pl_event_handler_disable (event_handler_t *handler);
void pl_event_handler_sync_release (event_handler_t *handler);
pthread_t pl_event_handler_tid (event_handler_t *handler);

#endif /* EVENT_HANDLER_H */
