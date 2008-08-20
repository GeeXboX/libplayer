/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2007-2008 Mathieu Schroeter <mathieu.schroeter@gamesover.ch>
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
#include <math.h>
#include <pthread.h>

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#ifdef HAVE_XINE
#include <xine.h>
#endif /* HAVE_XINE */

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "x11_common.h"

#define MODULE_NAME "x11"

struct x11_s {
  Display *display;
  Window window;
  void *data;
  int use_subwin;
  pthread_mutex_t mutex_display;
};

typedef struct screeninfo_s {
  int width;
  int height;
  double pixel_aspect;
  /* create a black background (only for MPlayer Xv video out) */
  Window win_black;
} screeninfo_t;

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


/**
 * Center the movie in the parent window and zoom for use the max of surface.
 */
void
zoom (player_t *player, int parentwidth, int parentheight, float aspect,
      int *x, int *y, int *width, int *height)
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
    /* fix aspect */
    if (aspect != 0.0)
      convert = aspect;
    else
      convert = (float) *width / (float) *height;

    *width = parentwidth;
    *height = (int) rintf ((float) *width / convert);

    if (*height > parentheight)
    {
      *height = parentheight;
      *width = (int) rintf ((float) *height * convert);
    }

    /* move to the center */
    *x = parentwidth / 2 - *width / 2;
    *y = parentheight / 2 - *height / 2;
  }

  plog (player, PLAYER_MSG_INFO, MODULE_NAME,
        "[zoom] x:%i y:%i w:%i h:%i r:%.2f", *x, *y, *width, *height, convert);
}

Display *
x11_get_display (x11_t *x11)
{
  if (!x11)
    return NULL;

  return x11->display;
}

Window
x11_get_window (x11_t *x11)
{
  if (!x11)
    return 0;

  return x11->window;
}

void *
x11_get_data (x11_t *x11)
{
  if (!x11)
    return NULL;

  return x11->data;
}

/**
 * Map and raise the window when a video is played.
 */
void
x11_map (player_t *player)
{
  x11_t *x11 = NULL;
  screeninfo_t *screeninfo;

  if (!player || !player->x11)
    return;

  x11 = player->x11;

  if (!x11->display)
    return;

  pthread_mutex_lock (&x11->mutex_display);

  if (x11->use_subwin)
  {
    screeninfo = (screeninfo_t *) player->x11->data;
    if (screeninfo && screeninfo->win_black > 0)
    {
      XWindowChanges changes;
      changes.x = 0;
      changes.y = 0;
      changes.width = player->w;
      changes.height = player->h;

      /* fix the size and offset */
      zoom (player, screeninfo->width, screeninfo->height, player->aspect,
            &changes.x, &changes.y, &changes.width, &changes.height);

      XConfigureWindow (x11->display, x11->window,
                        CWX | CWY | CWWidth | CWHeight, &changes);

      XMapRaised (x11->display, screeninfo->win_black);
    }
    else
      XMapRaised (x11->display, x11->window);
  }
  else
    XMapRaised (x11->display, x11->window);

  XSync (x11->display, False);
  pthread_mutex_unlock (&x11->mutex_display);

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "window mapped");
}

/**
 * Unmap the window when the video is ended or stopped.
 */
void
x11_unmap (player_t *player)
{
  x11_t *x11 = NULL;
  screeninfo_t *screeninfo;

  if (!player || !player->x11)
    return;

  x11 = player->x11;

  if (!x11->display)
    return;

  screeninfo = (screeninfo_t *) player->x11->data;

  pthread_mutex_lock (&x11->mutex_display);

  if (x11->use_subwin)
  {
    screeninfo = (screeninfo_t *) player->x11->data;
    if (screeninfo && screeninfo->win_black > 0)
      XUnmapWindow (x11->display, screeninfo->win_black);
    else
      XUnmapWindow (x11->display, x11->window);
  }
  else
    XUnmapWindow (x11->display, x11->window);

  XSync (x11->display, False);
  pthread_mutex_unlock (&x11->mutex_display);

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "window unmapped");
}

void
x11_uninit (player_t *player)
{
  x11_t *x11 = NULL;
  screeninfo_t *screeninfo;

  if (!player || !player->x11)
    return;

  x11 = player->x11;

  if (!x11->display)
    return;

  pthread_mutex_lock (&x11->mutex_display);
  XUnmapWindow (x11->display, x11->window);
  XDestroyWindow (x11->display, x11->window);

  if (x11->use_subwin)
  {
    screeninfo = (screeninfo_t *) player->x11->data;
    if (screeninfo && screeninfo->win_black > 0)
    {
      XUnmapWindow (x11->display, screeninfo->win_black);
      XDestroyWindow (x11->display, screeninfo->win_black);
      free (screeninfo);
    }
  }

  pthread_mutex_unlock (&x11->mutex_display);
  XCloseDisplay (x11->display);

  pthread_mutex_destroy (&x11->mutex_display);
  free (x11);

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "window destroyed");
}

#ifdef HAVE_XINE
static void
dest_size_cb(void *data, int video_width, int video_height,
             double video_pixel_aspect, int *dest_width,
             int *dest_height, double *dest_pixel_aspect)
{
  screeninfo_t *screeninfo;
  screeninfo = (screeninfo_t *) data;

  if (screeninfo)
  {
    *dest_width = screeninfo->width;
    *dest_height = screeninfo->height;
    *dest_pixel_aspect = screeninfo->pixel_aspect;
  }
  else
  {
    *dest_width = video_width;
    *dest_height = video_height;
    *dest_pixel_aspect = video_pixel_aspect;
  }
}

static void
frame_output_cb(void *data, int video_width, int video_height,
                double video_pixel_aspect, int *dest_x, int *dest_y,
                int *dest_width, int *dest_height,
                double *dest_pixel_aspect, int *win_x, int *win_y)
{
  screeninfo_t *screeninfo;
  screeninfo = (screeninfo_t *) data;

  *dest_x = 0;
  *dest_y = 0;
  *win_x = 0;
  *win_y = 0;

  if (screeninfo)
  {
    *dest_width = screeninfo->width;
    *dest_height = screeninfo->height;
    *dest_pixel_aspect = screeninfo->pixel_aspect;
  }
  else
  {
    *dest_width = video_width;
    *dest_height = video_height;
    *dest_pixel_aspect = video_pixel_aspect;
  }
}
#endif /* HAVE_XINE */

/**
 * Init and create a window for X11, XV or X11_SDL. Currently this window
 * is only fullscreen with a black background. The window is not mapped in
 * this function. Use x11_map() and x11_unmap() for show and hide this one.
 */
int
x11_init (player_t *player)
{
  x11_t *x11 = NULL;
#ifdef HAVE_XINE
  x11_visual_t *vis = NULL;
#endif /* HAVE_XINE */
  screeninfo_t *screeninfo;
  int screen, width, height;
  // double res_v, res_h;
  Atom XA_NO_BORDER;
  MWMHints mwmhints;
  XSetWindowAttributes atts;

  if (!player)
    return 0;

  player->x11 = calloc (1, sizeof (x11_t));
  x11 = player->x11;
  if (!x11)
    return 0;

  x11->display = NULL;
  x11->data = NULL;

  screeninfo = malloc (sizeof (screeninfo_t));
  if (!screeninfo)
  {
    free (x11);
    player->x11 = NULL;
    return 0;
  }
  screeninfo->win_black = 0;

  x11->display = XOpenDisplay (NULL);

  if (!x11->display)
  {
    free (x11);
    player->x11 = NULL;
    free (screeninfo);
    plog (player, PLAYER_MSG_WARNING, MODULE_NAME, "Failed to open display");
    return 0;
  }

  if (player->type == PLAYER_TYPE_MPLAYER
      && (player->vo == PLAYER_VO_XV ||
          player->vo == PLAYER_VO_GL ||
          player->vo == PLAYER_VO_AUTO))
  {
    x11->use_subwin = 1;
  }

  pthread_mutex_init (&x11->mutex_display, NULL);

  screen = XDefaultScreen (x11->display);

  /* the video will be in fullscreen */
  width = XDisplayWidth (x11->display, screen);
  height = XDisplayHeight (x11->display, screen);

  pthread_mutex_lock (&x11->mutex_display);

  atts.override_redirect = True;  /* window on top */
  atts.background_pixel = XBlackPixel (x11->display, screen); /* black background */

  /* remove borders */
  XA_NO_BORDER = XInternAtom (x11->display, "_MOTIF_WM_HINTS", False);
  mwmhints.flags = MWM_HINTS_DECORATIONS;
  mwmhints.decorations = 0;

  /* MPlayer and Xv use the hardware scale on all the surface. A second
   * window is then necessary for have a black background.
   */
  if (x11->use_subwin)
  {
    /* create a window for the black background */
    screeninfo->win_black = XCreateWindow (x11->display,
                                           XRootWindow (x11->display, screen),
                                           0, 0, width, height,
                                           0, 0,
                                           InputOutput,
                                           XDefaultVisual (x11->display, screen),
                                           CWOverrideRedirect | CWBackPixel, &atts);

    XChangeProperty (x11->display,
                     screeninfo->win_black,
                     XA_NO_BORDER, XA_NO_BORDER, 32,
                     PropModeReplace,
                     (unsigned char *) &mwmhints,
                     PROP_MWM_HINTS_ELEMENTS);

    /* create a window for the video out */
    x11->window = XCreateWindow (x11->display,
                                 screeninfo->win_black,
                                 0, 0, width, height,
                                 0, 0,
                                 InputOutput,
                                 XDefaultVisual (x11->display, screen),
                                 CWOverrideRedirect | CWBackPixel, &atts);

    XMapWindow (x11->display,  x11->window);
  }
  else
  {
    /* create a window for the video out */
    x11->window = XCreateWindow (x11->display,
                                 XRootWindow (x11->display, screen),
                                 0, 0, width, height,
                                 0, 0,
                                 InputOutput,
                                 XDefaultVisual (x11->display, screen),
                                 CWOverrideRedirect | CWBackPixel, &atts);
  }

  XChangeProperty (x11->display,
                   x11->window,
                   XA_NO_BORDER, XA_NO_BORDER, 32,
                   PropModeReplace,
                   (unsigned char *) &mwmhints,
                   PROP_MWM_HINTS_ELEMENTS);

  /* calcul pixel aspect */
  /*
  res_h = DisplayWidth (x11->display, screen) * 1000 /
          DisplayWidthMM (x11->display, screen);
  res_v = DisplayHeight (x11->display, screen) * 1000 /
          DisplayHeightMM (x11->display, screen);
  */

  XSync (x11->display, False);
  pthread_mutex_unlock (&x11->mutex_display);

  /* only for Xine */
  if (player->type == PLAYER_TYPE_XINE)
  {
#ifdef HAVE_XINE
    vis = malloc (sizeof (x11_visual_t));

    if (vis)
    {
      vis->display = x11->display;
      vis->screen = screen;
      vis->d = x11->window;
      vis->dest_size_cb = dest_size_cb;
      vis->frame_output_cb = frame_output_cb;

      screeninfo->width = width;
      screeninfo->height = height;
      // screeninfo->pixel_aspect = res_v / res_h;
      screeninfo->pixel_aspect = 1.0;

      vis->user_data = (void *) screeninfo;
    }

    x11->data = (void *) vis;
#endif /* HAVE_XINE */
  }
  /* only for MPlayer Xv */
  else if (x11->use_subwin)
  {
    screeninfo->width = width;
    screeninfo->height = height;
    // screeninfo->pixel_aspect = res_v / res_h;
    screeninfo->pixel_aspect = 1.0;

    x11->data = (void *) screeninfo;
  }
  /* others video out don't use data */
  else
  {
    free (screeninfo);
    x11->data = NULL;
  }

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "window initialized");

  return 1;
}
