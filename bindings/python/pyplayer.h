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

#ifndef PYPLAYER_H
#define PYPLAYER_H

#include <Python.h>
#include "structmember.h"

#include <stdio.h>
#include <player.h>

/* Python 2.5 64-bit support compatibility define */
#if PY_VERSION_HEX < 0x02050000 && !defined(PY_SSIZE_T_MIN)
typedef int Py_ssize_t;
#define PY_SSIZE_T_MAX INT_MAX
#define PY_SSIZE_T_MIN INT_MIN
#endif

/**********************************************************************
 * Player Object
 **********************************************************************/
typedef struct {
  PyObject_HEAD
  player_t *player;
} Player;

#define PLAYER_SELF ((Player*)self)

/**********************************************************************
 * MRL Object
 **********************************************************************/
typedef struct {
  PyObject_HEAD
  player_t *player;
  mrl_t *mrl;
} Mrl;

#define MRL_SELF ((Mrl*)self)

/* Forward declarations */
staticforward PyTypeObject Player_Type;
staticforward PyTypeObject Mrl_Type;

#endif /* PYPLAYER_H */
