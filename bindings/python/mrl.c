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
 * Mrl object implementation
 *****************************************************************************/

static PyObject *
Mrl_new (PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  PyErr_SetString (PyExc_TypeError,
                   "player.Mrl can't be instanciated by itself. " \
                   "You should use player.Player().mrl_new(...)." );
  return NULL;
}

static void
Mrl_dealloc (PyObject *self)
{
  mrl_free (MRL_SELF->player, MRL_SELF->mrl);
  PyObject_DEL (self);
}

static PyObject *
Mrl_add_sub (PyObject *self, PyObject *args)
{
  char *sub = NULL;

  if (!PyArg_ParseTuple (args, "s", &sub))
    return NULL;

  mrl_add_subtitle (MRL_SELF->player, MRL_SELF->mrl, sub);

  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
Mrl_get_type (PyObject *self, PyObject *args)
{
  mrl_type_t type = mrl_get_type (MRL_SELF->player, MRL_SELF->mrl);
  return Py_BuildValue ("i", type);
}

static PyObject *
Mrl_get_resource (PyObject *self, PyObject *args)
{
  mrl_resource_t res = mrl_get_resource (MRL_SELF->player, MRL_SELF->mrl);
  return Py_BuildValue ("i", res);
}

static PyObject *
Mrl_get_metadata (PyObject *self, PyObject *args)
{
  mrl_metadata_type_t m;
  PyObject *o;
  char *meta;

  if (!PyArg_ParseTuple (args, "i", &m))
    return NULL;

  meta = mrl_get_metadata (MRL_SELF->player, MRL_SELF->mrl, m);
  o = Py_BuildValue ("s", meta);
  free (meta);
  return o;
}

static PyObject *
Mrl_get_property (PyObject *self, PyObject *args)
{
  mrl_properties_type_t p;
  uint32_t prop;

  if (!PyArg_ParseTuple (args, "i", &p))
    return NULL;
  
  prop = mrl_get_property (MRL_SELF->player, MRL_SELF->mrl, p);
  return Py_BuildValue ("i", prop);
}

static PyObject *
Mrl_get_audio_codec (PyObject *self, PyObject *args)
{
  char *codec;
  PyObject *o;
  
  codec = mrl_get_audio_codec (MRL_SELF->player, MRL_SELF->mrl);
  o = Py_BuildValue ("s", codec);
  free (codec);
  return o;
}

static PyObject *
Mrl_get_video_codec (PyObject *self, PyObject *args)
{
  char *codec;
  PyObject *o;
  
  codec = mrl_get_video_codec (MRL_SELF->player, MRL_SELF->mrl);
  o = Py_BuildValue ("s", codec);
  free (codec);
  return o;
}

static PyObject *
Mrl_get_size (PyObject *self, PyObject *args)
{
  off_t size = mrl_get_size (MRL_SELF->player, MRL_SELF->mrl);
  return Py_BuildValue ("i", size);
}

/* Method table */
static PyMethodDef Mrl_methods[] = {

  { "add_sub", Mrl_add_sub, METH_VARARGS,
    "add_sub(filename) Add a subtitle to the MRL." },
  { "get_type", Mrl_get_type, METH_NOARGS,
    "get_type() Return the MRL type." },
  { "get_resource", Mrl_get_resource, METH_NOARGS,
    "get_resource() Return the MRL resource type." },
  { "get_meta", Mrl_get_metadata, METH_VARARGS,
    "get_meta(meta) Return the requested metadata." },
  { "get_prop", Mrl_get_property, METH_VARARGS,
    "get_prop(prop) Return the requested property." },
  { "get_acodec", Mrl_get_audio_codec, METH_NOARGS,
    "get_acodec() Return the MRL audio codec." },
  { "get_vcodec", Mrl_get_video_codec, METH_NOARGS,
    "get_vcodec() Return the MRL video codec." },
  { "get_size", Mrl_get_size, METH_NOARGS,
    "get_size() Return the MRL size." },
  { NULL, NULL, 0, NULL },
};
    
static PyTypeObject Mrl_Type = {
  PyObject_HEAD_INIT (NULL)
  0,                                        /* ob_size */
  "player.Mrl",                             /* tp_name */
  sizeof (Mrl_Type),                        /* tp_basicsize */
  0,                                        /* tp_itemsize */
  (destructor) Mrl_dealloc,                 /* tp_dealloc */
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
  "player.Mrl object",                      /* tp_doc */
  0,                                        /* tp_traverse */
  0,                                        /* tp_clear */
  0,                                        /* tp_richcompare */
  0,                                        /* tp_weaklistoffset */
  0,                                        /* tp_iter */
  0,                                        /* tp_iternext */
  Mrl_methods,                              /* tp_methods */
  0,                                        /* tp_members */
  0,                                        /* tp_getset */
  0,                                        /* tp_base */
  0,                                        /* tp_dict */
  0,                                        /* tp_descr_get */
  0,                                        /* tp_descr_set */
  0,                                        /* tp_dictoffset */
  0,                                        /* tp_init */
  0,                                        /* tp_alloc */
  Mrl_new,                                  /* tp_new */
};
