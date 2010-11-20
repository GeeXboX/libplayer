/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2010 Mathieu Schroeter <mathieu@schroetersa.ch>
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

#ifndef PLAYER_WINDOW_COMMON_H
#define PLAYER_WINDOW_COMMON_H

/**
 * \file window_common.h
 *
 * Internal window backend API header.
 */

#include "window.h"

typedef enum window_backend {
  WIN_BACKEND_NULL = 0,
  WIN_BACKEND_AUTO,
  WIN_BACKEND_XCB,
  WIN_BACKEND_GDI, /* TODO */
} window_backend_t;

/**
 * \brief Functions for the backend.
 */
typedef struct window_funcs_s {
  /**
   * \brief Init the backend.
   *
   * \param[in] win         Window handle.
   * \return !=0 on error.
   */
  int (*init) (struct window_s *win);

  /**
   * \brief Uninit the backend.
   *
   * \param[in] win         Window handle.
   */
  void (*uninit) (struct window_s *win);

  /**
   * \brief Map and raise the window.
   *
   * \param[in] win         Window handle.
   */
  void (*map) (struct window_s *win);

  /**
   * \brief Unmap the window.
   *
   * \param[in] win         Window handle.
   */
  void (*unmap) (struct window_s *win);

  /**
   * \brief Refresh the size of the video window.
   *
   * It uses the player->aspect attribute which depends of the movie and
   * adjusts the video window in order to use the optimal size.
   *
   * \param[in] win         Window handle.
   */
  void (*resize) (struct window_s *win);

  /**
   * \brief Retrieve the window ID of the video window.
   *
   * The video window is always different that player->winid which is the
   * parent.
   *
   * \param[in] win         Window handle.
   * \return the video window ID.
   */
  uint32_t (*winid_get) (struct window_s *win);

  /**
   * \brief Retrieve internal data for the wrapper.
   *
   * Note that most of wrappers do not use that. Only xine is using this data
   * for its video output driver.
   *
   * \param[in] win         Window handle.
   * \return the internal data.
   */
  void *(*data_get) (struct window_s *win);

  /**
   * \brief Retrieve the position of the video in the window.
   *
   * This is the position of the video in the window and not the position
   * of the window. There is a difference in the case where the size
   * between the video and the window is not the same (black borders).
   *
   * \param[in] win         Window handle.
   * \param[out] x          Absolute X coordinate.
   * \param[out] y          Absolute Y coordinate.
   */
  void (*video_pos_get) (struct window_s *win, int *x, int *y);

  /**
   * \brief Set new sizes and coordinates to the window.
   *
   * \param[in] win         Window handle.
   * \param[in] x           X coordinate.
   * \param[in] y           Y coordinate.
   * \param[in] w           Width.
   * \param[in] h           Height.
   * \param[in] flags       Parameters to consider.
   */
  void (*win_props_set) (struct window_s *win,
                         int x, int y, int w, int h, int flags);

  /**
   * \brief Retrieve the VDPAU capabilities of the GPU (nVidia only).
   *
   * \param[in] win         Window handle.
   * \return the capabilities (see window_vdpau_caps_t).
   */
  int (*vdpau_caps_get) (struct window_s *win);

} window_funcs_t;

struct window_s {
  player_t        *player;
  window_funcs_t  *funcs;
  window_backend_t backend;
  void            *backend_data;
};

#endif /* PLAYER_WINDOW_COMMON_H */
