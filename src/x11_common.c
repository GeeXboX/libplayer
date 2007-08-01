/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2007 Mathieu Schroeter <mathieu.schroeter@gamesover.ch>
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

#include <stdlib.h>

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <xine.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "x11_common.h"

#define MODULE_NAME "x11"

/* for no border with a X window */
typedef struct {
  uint32_t flags;
  uint32_t functions;
  uint32_t decorations;
  int32_t input_mode;
  uint32_t status;
} MWMHints;

#define MWM_HINTS_DECORATIONS     (1L << 1)
#define PROP_MWM_HINTS_ELEMENTS   5

/* width and height for the fullscreen */
static int g_x11_width;
static int g_x11_height;

static double g_x11_pixel_aspect;


/**
 * Map and raise the window when a video is played.
 */
void
x11_map (player_t *player)
{
  x11_t *x11 = NULL;

  if (!player || !player->x11)
    return;

  x11 = player->x11;

  if (!x11->display)
    return;

  XLockDisplay (x11->display);
  XMapRaised (x11->display, x11->window);
  XSync (x11->display, False);
  XUnlockDisplay (x11->display);

  plog (MODULE_NAME, "window mapped");
}

/**
 * Unmap the window when the video is ended or stopped.
 */
void
x11_unmap (player_t *player)
{
  x11_t *x11 = NULL;

  if (!player || !player->x11)
    return;

  x11 = player->x11;

  if (!x11->display)
    return;

  XLockDisplay (x11->display);
  XUnmapWindow (x11->display, x11->window);
  XSync (x11->display, False);
  XUnlockDisplay (x11->display);

  plog (MODULE_NAME, "window unmapped");
}

void
x11_uninit (player_t *player)
{
  x11_t *x11 = NULL;

  if (!player || !player->x11)
    return;

  x11 = player->x11;

  if (!x11->display)
    return;

  XLockDisplay (x11->display);
  XUnmapWindow (x11->display,  x11->window);
  XDestroyWindow (x11->display, x11->window);
  XUnlockDisplay (x11->display);
  XCloseDisplay (x11->display);

  free (x11);

  plog (MODULE_NAME, "window destroyed");
}

static void
dest_size_cb(void *data, int video_width, int video_height,
             double video_pixel_aspect, int *dest_width,
             int *dest_height, double *dest_pixel_aspect)
{
  *dest_width = g_x11_width;
  *dest_height = g_x11_height;
  *dest_pixel_aspect = g_x11_pixel_aspect;
}

static void
frame_output_cb(void *data, int video_width, int video_height,
                double video_pixel_aspect, int *dest_x, int *dest_y,
                int *dest_width, int *dest_height,
                double *dest_pixel_aspect, int *win_x, int *win_y)
{
  *dest_x = 0;
  *dest_y = 0;
  *win_x = 0;
  *win_y = 0;
  *dest_width = g_x11_width;
  *dest_height = g_x11_height;
  *dest_pixel_aspect = g_x11_pixel_aspect;
}

/**
 * Init and create a window for X11, XV or X11_SDL. Currently this window
 * is only fullscreen with a black background. The window is not mapped in
 * this function. Use x11_map() and x11_unmap() for show and hide this one.
 */
int
x11_init (player_t *player)
{
  x11_t *x11 = NULL;
  x11_visual_t *vis = NULL;
  int screen;
  double res_v, res_h;
  Atom XA_NO_BORDER;
  MWMHints mwmhints;
  XSetWindowAttributes atts;

  if (!player)
    return 0;

  player->x11 = malloc (sizeof (x11_t));
  x11 = player->x11;

  x11->display = NULL;
  x11->data = NULL;

  if (!XInitThreads ()) {
    plog (MODULE_NAME, "Failed to init for X11");
    return 0;
  }

  x11->display = XOpenDisplay (NULL);

  if (!x11->display)
    return 0;

  screen = XDefaultScreen (x11->display);

  /* the video will be in fullscreen */
  g_x11_width = DisplayWidth (x11->display, screen);
  g_x11_height = DisplayHeight (x11->display, screen);

  XLockDisplay (x11->display);

  atts.override_redirect = True;  /* window on top */
  atts.background_pixel = 0;      /* black background */

  /* create a window for the video out */
  x11->window = XCreateWindow (x11->display, DefaultRootWindow (x11->display),
                               0, 0, g_x11_width, g_x11_height, 0, 0,
                               InputOutput, DefaultVisual (x11->display, screen),
                               CWOverrideRedirect | CWBackPixel, &atts);

  /* remove borders for the fullscreen */
  XA_NO_BORDER = XInternAtom (x11->display, "_MOTIF_WM_HINTS", False);
  mwmhints.flags = MWM_HINTS_DECORATIONS;
  mwmhints.decorations = 0;
  XChangeProperty (x11->display, x11->window, XA_NO_BORDER, XA_NO_BORDER, 32,
                   PropModeReplace, (unsigned char *) &mwmhints,
                   PROP_MWM_HINTS_ELEMENTS);

  /* calcul pixel aspect */
  res_h = DisplayWidth (x11->display, screen) * 1000 /
          DisplayWidthMM (x11->display, screen);
  res_v = DisplayHeight (x11->display, screen) * 1000 /
          DisplayHeightMM (x11->display, screen);
  g_x11_pixel_aspect = res_v / res_h;

  XSync (x11->display, False);
  XUnlockDisplay (x11->display);

  if (player->type == PLAYER_TYPE_XINE) {
    vis = malloc (sizeof (x11_visual_t));

    if (vis) {
      vis->display = x11->display;
      vis->screen = screen;
      vis->d = x11->window;
      vis->dest_size_cb = dest_size_cb;
      vis->frame_output_cb = frame_output_cb;
      vis->user_data = NULL;
    }

    x11->data = (void *) vis;
  }
  else
    x11->data = NULL;

  plog (MODULE_NAME, "window initialized");

  return 1;
}
