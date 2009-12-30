/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2007-2009 Mathieu Schroeter <mathieu.schroeter@gamesover.ch>
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

#define _GNU_SOURCE
#include <stdlib.h>
#include <math.h>
#include <pthread.h>

#include <xcb/xcb.h>

#ifdef HAVE_XINE
#include <xine.h>
#endif /* HAVE_XINE */

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "x11_common.h"

#define MODULE_NAME "x11"

static const uint32_t val_raised[] = { XCB_STACK_MODE_ABOVE };

struct x11_s {
  xcb_connection_t *display;
  xcb_window_t      win_video;
  xcb_window_t      win_black; /* black background  (use_subwin to 1) */
  xcb_window_t      win_trans; /* InputOnly windows (use_subwin to 1) */
  xcb_screen_t     *screen;
  int use_subwin;

  int16_t  x, y;       /* position set by the user */
  uint16_t w, h;       /* size set by the user */
  uint16_t width;      /* screen width */
  uint16_t height;     /* screen height */
  pthread_mutex_t mutex;

  int16_t  x_vid, y_vid;  /* position of win_video */
  uint16_t w_vid, h_vid;  /* size of win_video */

  double pixel_aspect;
  void *data;
};


/*
 * Center the movie in the parent window and zoom to use the max of surface.
 */
static void
zoom (player_t *player, uint16_t parentwidth, uint16_t parentheight,
      float aspect, int16_t *x, int16_t *y, uint16_t *width, uint16_t *height)
{
  float convert;

  /* use all the surface */
  if (!*width || !*height)
  {
    *width = parentwidth;
    *height = parentheight;
    *x = 0;
    *y = 0;
    convert = 1.0;
  }
  /* or calcul the best size */
  else
  {
    /* fix aspect ratio */
    if (aspect != 0.0)
      convert = aspect;
    else
      convert = (float) *width / (float) *height;

    *width = parentwidth;
    *height = (uint16_t) rintf ((float) *width / convert);

    if (*height > parentheight)
    {
      *height = parentheight;
      *width = (uint16_t) rintf ((float) *height * convert);
    }

    /* move to the center */
    *x = parentwidth / 2 - *width / 2;
    *y = parentheight / 2 - *height / 2;
  }

  pl_log (player, PLAYER_MSG_INFO,
          MODULE_NAME, "[zoom] x:%i y:%i w:%u h:%u r:%.2f",
                       *x, *y, *width, *height, convert);
}

uint32_t
pl_x11_get_window (x11_t *x11)
{
  if (!x11)
    return 0;

  return (uint32_t) x11->win_video;
}

void *
pl_x11_get_data (x11_t *x11)
{
  if (!x11)
    return NULL;

  return x11->data;
}

void
pl_x11_set_winprops (x11_t *x11, int x, int y, int w, int h, int flags)
{
  if (!x11)
    return;

  pthread_mutex_lock (&x11->mutex);
  if (flags & X11_PROPERTY_X)
    x11->x = x;
  if (flags & X11_PROPERTY_Y)
    x11->y = y;
  if (flags & X11_PROPERTY_W)
    x11->w = w;
  if (flags & X11_PROPERTY_H)
    x11->h = h;
  pthread_mutex_unlock (&x11->mutex);
}

void
pl_x11_get_video_pos (x11_t *x11, int *x, int *y)
{
  if (!x11 || (!x && !y))
    return;

  pthread_mutex_lock (&x11->mutex);
  if (x)
    *x = x11->x_vid + (x11->use_subwin ? x11->x : 0);
  if (y)
    *y = x11->y_vid + (x11->use_subwin ? x11->y : 0);
  pthread_mutex_unlock (&x11->mutex);
}

#define PL_X11_CHANGES_X 0
#define PL_X11_CHANGES_Y 1
#define PL_X11_CHANGES_W 2
#define PL_X11_CHANGES_H 3

void
pl_x11_resize (player_t *player)
{
  x11_t *x11 = NULL;
  uint32_t changes[] = { 0, 0, 0, 0 }; /* x, y, w, h */
  int16_t x, y;
  uint16_t width, height;

  if (!player || !player->x11)
    return;

  x11 = player->x11;

  if (!x11->display)
    return;

  pthread_mutex_lock (&x11->mutex);
  width  = x11->width;
  height = x11->height;

  if (player->winid)
  {
    xcb_get_geometry_cookie_t cookie;
    xcb_get_geometry_reply_t *geom;

    cookie = xcb_get_geometry (x11->display, (xcb_window_t) player->winid);
    geom = xcb_get_geometry_reply (x11->display, cookie, NULL);
    if (geom)
    {
      width  = geom->width;
      height = geom->height;
      free (geom);
    }

    x11->width  = width;
    x11->height = height;
  }

  /* window position and size set by the user */
  x = x11->x;
  y = x11->y;
  if (x11->w)
    width = x11->w;
  if (x11->h)
    height = x11->h;
  pthread_mutex_unlock (&x11->mutex);

  if (x11->use_subwin && x11->win_black)
  {
    /* reconfigure black and trans windows */
    changes[PL_X11_CHANGES_X] = x;
    changes[PL_X11_CHANGES_Y] = y;
    changes[PL_X11_CHANGES_W] = width;
    changes[PL_X11_CHANGES_H] = height;
    xcb_configure_window (x11->display, x11->win_black,
                          XCB_CONFIG_WINDOW_X     |
                          XCB_CONFIG_WINDOW_Y     |
                          XCB_CONFIG_WINDOW_WIDTH |
                          XCB_CONFIG_WINDOW_HEIGHT,
                          changes);
    if (x11->win_trans)
      xcb_configure_window (x11->display, x11->win_trans,
                            XCB_CONFIG_WINDOW_WIDTH |
                            XCB_CONFIG_WINDOW_HEIGHT,
                            changes + 2);

    x11->x_vid = 0;
    x11->y_vid = 0;
    x11->w_vid = player->w;
    x11->h_vid = player->h;

    /* fix the size and offset */
    zoom (player, width, height,
          player->aspect, &x11->x_vid, &x11->y_vid, &x11->w_vid, &x11->h_vid);

    changes[PL_X11_CHANGES_X] = (uint32_t) x11->x_vid;
    changes[PL_X11_CHANGES_Y] = (uint32_t) x11->y_vid;
    changes[PL_X11_CHANGES_W] = x11->w_vid;
    changes[PL_X11_CHANGES_H] = x11->h_vid;
  }
  else
  {
    x11->x_vid = x;
    x11->y_vid = y;
    x11->w_vid = width;
    x11->h_vid = height;

    changes[PL_X11_CHANGES_X] = x;
    changes[PL_X11_CHANGES_Y] = y;
    changes[PL_X11_CHANGES_W] = width;
    changes[PL_X11_CHANGES_H] = height;
  }

    xcb_configure_window (x11->display, x11->win_video,
                          XCB_CONFIG_WINDOW_X     |
                          XCB_CONFIG_WINDOW_Y     |
                          XCB_CONFIG_WINDOW_WIDTH |
                          XCB_CONFIG_WINDOW_HEIGHT,
                          changes);

  xcb_flush (x11->display);

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "window resized");
}

void
pl_x11_map (player_t *player)
{
  x11_t *x11 = NULL;

  if (!player || !player->x11)
    return;

  x11 = player->x11;

  if (!x11->display)
    return;

  pl_x11_resize (player);

  if (x11->use_subwin && x11->win_black)
  {
    xcb_configure_window (x11->display, x11->win_black,
                          XCB_CONFIG_WINDOW_STACK_MODE, val_raised);
    xcb_map_window (x11->display, x11->win_black);
  }
  else
  {
    xcb_configure_window (x11->display, x11->win_video,
                          XCB_CONFIG_WINDOW_STACK_MODE, val_raised);
    xcb_map_window (x11->display, x11->win_video);
  }

  xcb_flush (x11->display);

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "window mapped");
}

void
pl_x11_unmap (player_t *player)
{
  x11_t *x11 = NULL;

  if (!player || !player->x11)
    return;

  x11 = player->x11;

  if (!x11->display)
    return;

  if (x11->use_subwin && x11->win_black)
    xcb_unmap_window (x11->display, x11->win_black);
  else
    xcb_unmap_window (x11->display, x11->win_video);

  xcb_flush (x11->display);

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "window unmapped");
}

void
pl_x11_uninit (player_t *player)
{
  x11_t *x11 = NULL;

  if (!player || !player->x11)
    return;

  x11 = player->x11;

  if (!x11->display)
    return;

  xcb_unmap_window (x11->display, x11->win_video);
  xcb_destroy_window (x11->display, x11->win_video);

  if (x11->win_trans)
  {
    xcb_unmap_window (x11->display, x11->win_trans);
    xcb_destroy_window (x11->display, x11->win_trans);
  }
  if (x11->win_black)
  {
    xcb_unmap_window (x11->display, x11->win_black);
    xcb_destroy_window (x11->display, x11->win_black);
  }

  xcb_disconnect (x11->display);

  if (x11->data)
    free (x11->data);

  pthread_mutex_destroy (&x11->mutex);
  free (x11);
  player->x11 = NULL;

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "window destroyed");
}

#ifdef HAVE_XINE
static inline void
xine_dest_props (x11_t *x11,
                 int video_width, int video_height, double video_pixel_aspect,
                 int *dest_width, int *dest_height, double *dest_pixel_aspect)
{
  if (x11)
  {
    pthread_mutex_lock (&x11->mutex);
    if (x11->w)
      *dest_width = x11->w;
    else
      *dest_width = x11->width;

    if (x11->h)
      *dest_height = x11->h;
    else
      *dest_height = x11->height;
    pthread_mutex_unlock (&x11->mutex);

    *dest_pixel_aspect = x11->pixel_aspect;
  }
  else
  {
    *dest_width  = video_width;
    *dest_height = video_height;
    *dest_pixel_aspect = video_pixel_aspect;
  }
}

static void
xine_dest_size_cb (void *data, int video_width, int video_height,
                   double video_pixel_aspect, int *dest_width,
                   int *dest_height, double *dest_pixel_aspect)
{
  x11_t *x11 = data;

  xine_dest_props (x11,
                   video_width, video_height, video_pixel_aspect,
                   dest_width, dest_height, dest_pixel_aspect);
}

static void
xine_frame_output_cb (void *data, int video_width, int video_height,
                      double video_pixel_aspect, int *dest_x, int *dest_y,
                      int *dest_width, int *dest_height,
                      double *dest_pixel_aspect, int *win_x, int *win_y)
{
  x11_t *x11 = data;

  *dest_x = 0;
  *dest_y = 0;
  *win_x  = 0;
  *win_y  = 0;

  xine_dest_props (x11,
                   video_width, video_height, video_pixel_aspect,
                   dest_width, dest_height, dest_pixel_aspect);
}
#endif /* HAVE_XINE */

static xcb_screen_t *
screen_of_display (xcb_connection_t *c, int screen)
{
  xcb_screen_iterator_t iter;

  iter = xcb_setup_roots_iterator (xcb_get_setup (c));
  for (; iter.rem; --screen, xcb_screen_next (&iter))
    if (!screen)
      return iter.data;

  return NULL;
}

/*
 * This X11 initialization seems to not work very well with Compiz Window
 * Manager and maybe all related managers. The main problem seems to be
 * the override_redirect attribute. But it works fine when the main window
 * is attached to an other (see player_init(), winid parameter).
 */
int
pl_x11_init (player_t *player)
{
  x11_t *x11 = NULL;
  xcb_window_t win_root;
  int screen;
  xcb_visualid_t visual;
  uint32_t attributes[] = { 0, 1 }; /* black_pixel, override_redirect */

  if (!player)
    return 0;

  player->x11 = calloc (1, sizeof (x11_t));
  x11 = player->x11;
  if (!x11)
    return 0;

  x11->display = xcb_connect (NULL, &screen);
  if (!x11->display)
  {
    pl_log (player, PLAYER_MSG_WARNING, MODULE_NAME, "Failed to open display");
    goto err;
  }

  if (player->type == PLAYER_TYPE_MPLAYER)
    x11->use_subwin = 1;

  pthread_mutex_init (&x11->mutex, NULL);

  x11->screen = screen_of_display (x11->display, screen);
  if (!x11->screen)
  {
    pl_log (player, PLAYER_MSG_WARNING,
            MODULE_NAME, "Failed to found the screen");
    goto err;
  }

  attributes[0] = x11->screen->black_pixel;

  win_root = (xcb_window_t) player->winid;
  if (!win_root)
  {
    x11->width  = x11->screen->width_in_pixels;
    x11->height = x11->screen->height_in_pixels;
    visual      = x11->screen->root_visual;
    win_root    = x11->screen->root;
  }
  else
  {
    xcb_get_geometry_cookie_t cookie_geom;
    xcb_get_geometry_reply_t *geom;
    xcb_get_window_attributes_cookie_t cookie_atts;
    xcb_get_window_attributes_reply_t *atts;

    cookie_geom = xcb_get_geometry (x11->display, win_root);
    geom = xcb_get_geometry_reply (x11->display, cookie_geom, NULL);
    if (geom)
    {
      x11->width  = geom->width;
      x11->height = geom->height;
      free (geom);
    }

    cookie_atts = xcb_get_window_attributes (x11->display, win_root);
    atts = xcb_get_window_attributes_reply (x11->display, cookie_atts, NULL);
    if (atts)
    {
      visual = atts->visual;
      free (atts);
    }
  }

  x11->w_vid = x11->width;
  x11->h_vid = x11->height;

  /*
   * Some video outputs of MPlayer (like Xv and OpenGL), use the hardware
   * scaling on all the surface (and not accordingly to the video aspect
   * ratio). In this case, a second window is necessary in order to have a
   * black background.
   * Aspect ratio will be changed by the resizing of the win_video window.
   */
  if (x11->use_subwin)
  {
    /* create a window for the black background */
    x11->win_black = xcb_generate_id (x11->display);
    xcb_create_window (x11->display, XCB_COPY_FROM_PARENT, x11->win_black,
                       win_root, 0, 0, x11->width, x11->height, 0,
                       XCB_WINDOW_CLASS_INPUT_OUTPUT, visual,
                       XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT,
                       attributes);

    /* create a window for the video out */
    x11->win_video = xcb_generate_id (x11->display);
    xcb_create_window (x11->display, XCB_COPY_FROM_PARENT, x11->win_video,
                       x11->win_black, 0, 0, x11->width, x11->height, 0,
                       XCB_WINDOW_CLASS_INPUT_OUTPUT, visual,
                       XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT,
                       attributes);

    xcb_map_window (x11->display, x11->win_video);

    /*
     * Transparent window to catch all events in order to prevent sending
     * events to MPlayer.
     */
    x11->win_trans = xcb_generate_id (x11->display);
    xcb_create_window (x11->display, XCB_COPY_FROM_PARENT, x11->win_trans,
                       x11->win_black, 0, 0, x11->width, x11->height, 0,
                       XCB_WINDOW_CLASS_INPUT_ONLY, visual,
                       XCB_CW_OVERRIDE_REDIRECT, attributes + 1);

    xcb_configure_window (x11->display, x11->win_trans,
                          XCB_CONFIG_WINDOW_STACK_MODE, val_raised);
    xcb_map_window (x11->display, x11->win_trans);
  }
  else
  {
    /* create a window for the video out */
    x11->win_video = xcb_generate_id (x11->display);
    xcb_create_window (x11->display, XCB_COPY_FROM_PARENT, x11->win_video,
                       win_root, 0, 0, x11->width, x11->height, 0,
                       XCB_WINDOW_CLASS_INPUT_OUTPUT, visual,
                       XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT,
                       attributes);
  }

  xcb_flush (x11->display);

  x11->pixel_aspect = 1.0;

  if (player->type == PLAYER_TYPE_XINE)
  {
#ifdef HAVE_XINE
    xcb_visual_t *vis = calloc (1, sizeof (xcb_visual_t));

    if (vis)
    {
      vis->connection      = x11->display;
      vis->screen          = x11->screen;
      vis->window          = x11->win_video;
      vis->dest_size_cb    = xine_dest_size_cb;
      vis->frame_output_cb = xine_frame_output_cb;
      vis->user_data       = (void *) x11;
    }

    x11->data = (void *) vis;
#endif /* HAVE_XINE */
  }

  pl_log (player, PLAYER_MSG_INFO, MODULE_NAME, "window initialized");
  return 1;

 err:
  free (x11);
  player->x11 = NULL;
  return 0;
}
