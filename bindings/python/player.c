/*
 * GeeXboX libplayer python binding module
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

#include "pyplayer.h"

/*****************************************************************************
 * Player object implementation
 *****************************************************************************/

static PyObject *
Player_new (PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  Player *self;
  player_type_t t = PLAYER_TYPE_DUMMY;
  player_ao_t ao = PLAYER_AO_NULL;
  player_vo_t vo = PLAYER_VO_NULL;
  player_verbosity_level_t verbosity = PLAYER_MSG_INFO;
    
  if (!PyArg_ParseTuple (args, "iiii", &t, &ao, &vo, &verbosity))
    fprintf (stderr, "Error Parsing Args\n");
  
  self = PyObject_New (Player, &Player_Type);

  Py_BEGIN_ALLOW_THREADS
    PLAYER_SELF->player = player_init (t, ao, vo, verbosity, 0, NULL);
  Py_END_ALLOW_THREADS
    Py_INCREF (self);
  
  return (PyObject *) self;
}

static void
Player_dealloc (PyObject *self)
{
  player_uninit (PLAYER_SELF->player);
  PyObject_DEL (self);
}

static PyObject *
Player_set_verbosity (PyObject *self, PyObject *args)
{
  player_verbosity_level_t level;

  if (!PyArg_ParseTuple (args, "i", &level))
    return NULL;

  player_set_verbosity (PLAYER_SELF->player, level);

  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
Player_mrl_new_file (PyObject *self, PyObject *args)
{
  mrl_resource_local_args_t *res;
  mrl_t *mrl;
  char *uri = NULL;
  Mrl *o;
  
  if (!PyArg_ParseTuple (args, "s", &uri))
    return NULL;

  res = malloc (sizeof (mrl_resource_local_args_t));
  res->location = strdup (uri);

  mrl = mrl_new (PLAYER_SELF->player, MRL_RESOURCE_FILE, res);

  if (!mrl)
    return NULL;
    
  o = PyObject_New (Mrl, &Mrl_Type);
  o->player = PLAYER_SELF->player;
  o->mrl = mrl;

  Py_INCREF (o);
  return (PyObject *) o;
}

static PyObject *
Player_set_mrl (PyObject *self, PyObject *args)
{
  Mrl *o = NULL;
  
  if (!PyArg_ParseTuple (args, "O", &o))
    return NULL;

  if (!PyObject_TypeCheck (o, &Mrl_Type))
    return NULL;
  
  player_mrl_set (PLAYER_SELF->player, o->mrl);
  
  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
Player_playback_start (PyObject *self, PyObject *args)
{
  player_playback_start (PLAYER_SELF->player);

  Py_INCREF (Py_None);
  return Py_None;
}

/* Method table */
static PyMethodDef Player_methods[] = {
  { "set_verbosity", Player_set_verbosity, METH_VARARGS,
    "set_verbosity(level=int) Set the player verbosity level" },
  { "mrl_new_file", Player_mrl_new_file, METH_VARARGS,
    "mrl_new_file(filename) Create a new MRL object." },
  { "set_mrl", Player_set_mrl, METH_VARARGS,
    "set_mrl(mrl) Set player current MRL." },
  { "play", Player_playback_start, METH_NOARGS,
    "play() Start playback." },
  { NULL, NULL, 0, NULL },
};
    
static PyTypeObject Player_Type = {
  PyObject_HEAD_INIT( NULL )
  0,                                        /* ob_size */
  "player.Player",                          /* tp_name */
  sizeof (Player_Type),                     /* tp_basicsize */
  0,                                        /* tp_itemsize */
  (destructor) Player_dealloc,              /* tp_dealloc */
  0,                                        /* tp_print */
  0,                                        /* tp_getattr */
  0,                                        /* tp_setattr */
  0,                                        /* tp_compare */
  0,                                        /* tp_repr */
  0,                                        /* tp_as_number */
  0,                                        /* tp_as_sequence */
  0,                                        /* tp_as_mapping */
  0,                                        /* tp_hash */
  0,                                        /* tp_call */
  0,                                        /* tp_str */
  0,                                        /* tp_getattro */
  0,                                        /* tp_setattro */
  0,                                        /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
  "player.Player object",                   /* tp_doc */
  0,                                        /* tp_traverse */
  0,                                        /* tp_clear */
  0,                                        /* tp_richcompare */
  0,                                        /* tp_weaklistoffset */
  0,                                        /* tp_iter */
  0,                                        /* tp_iternext */
  Player_methods,                           /* tp_methods */
  0,                                        /* tp_members */
  0,                                        /* tp_getset */
  0,                                        /* tp_base */
  0,                                        /* tp_dict */
  0,                                        /* tp_descr_get */
  0,                                        /* tp_descr_set */
  0,                                        /* tp_dictoffset */
  0,                                        /* tp_init */
  0,                                        /* tp_alloc */
  Player_new,                               /* tp_new */
};
