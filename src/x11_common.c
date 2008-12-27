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
  Window win_video;
  void *data;
  int use_subwin;
  int x, y;       /* position set by the user */
  int w, h;       /* size set by the user */
  pthread_mutex_t mutex_display;
};

typedef struct screeninfo_s {
  int width;
  int height;
  double pixel_aspect;
  Window win_black; /* black background (use_subwin to 1) */
  Window win_trans; /* InputOnly window to catch events (use_subwin to 1) */
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


/*
 * Center the movie in the parent window and zoom to use the max of surface.
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
    /* fix aspect ratio */
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

  return x11->win_video;
}

void *
x11_get_data (x11_t *x11)
{
  if (!x11)
    return NULL;

  return x11->data;
}

void
x11_set_winprops (x11_t *x11, int x, int y, int w, int h, int flags)
{
  if (!x11)
    return;

  pthread_mutex_lock (&x11->mutex_display);
  if (flags & X11_PROPERTY_X)
    x11->x = x;
  if (flags & X11_PROPERTY_Y)
    x11->y = y;
  if (flags & X11_PROPERTY_W)
    x11->w = w;
  if (flags & X11_PROPERTY_H)
    x11->h = h;
  pthread_mutex_unlock (&x11->mutex_display);
}

static screeninfo_t *
x11_get_screeninfo (player_t *player)
{
#ifdef HAVE_XINE
  x11_visual_t *vis;
#endif /* HAVE_XINE */

  if (!player || !player->x11)
    return NULL;

  if (player->type == PLAYER_TYPE_XINE)
  {
#ifdef HAVE_XINE
    vis = player->x11->data;
    if (vis)
      return vis->user_data;
#endif /* HAVE_XINE */
  }
  else
    return player->x11->data;

  return NULL;
}

void
x11_resize (player_t *player)
{
  x11_t *x11 = NULL;
  screeninfo_t *screeninfo = NULL;
  XWindowChanges changes;
  int x, y;
  int width, height;

  if (!player || !player->x11)
    return;

  x11 = player->x11;

  if (!x11->display)
    return;

  pthread_mutex_lock (&x11->mutex_display);

  screeninfo = x11_get_screeninfo (player);

  if (player->winid)
  {
    XWindowAttributes atts;
    XGetWindowAttributes (x11->display, player->winid, &atts);
    width = atts.width;
    height = atts.height;

    if (screeninfo)
    {
      screeninfo->width = width;
      screeninfo->height = height;
    }
  }
  else if (screeninfo)
  {
    width = screeninfo->width;
    height = screeninfo->height;
  }

  /* window position and size set by the user */
  x = x11->x;
  y = x11->y;
  if (x11->w)
    width = x11->w;
  if (x11->h)
    height = x11->h;

  if (x11->use_subwin)
  {
    if (screeninfo && screeninfo->win_black)
    {
      changes.x = 0;
      changes.y = 0;
      changes.width = player->w;
      changes.height = player->h;

      /* fix the size and offset */
      zoom (player, width, height, player->aspect,
            &changes.x, &changes.y, &changes.width, &changes.height);

      XConfigureWindow (x11->display, x11->win_video,
                        CWX | CWY | CWWidth | CWHeight, &changes);

      /* reconfigure black and trans windows */
      changes.x = x;
      changes.y = y;
      changes.width = width;
      changes.height = height;
      XConfigureWindow (x11->display, screeninfo->win_black,
                        CWX | CWY | CWWidth | CWHeight, &changes);
      if (screeninfo->win_trans)
        XConfigureWindow (x11->display, screeninfo->win_trans,
                          CWWidth | CWHeight, &changes);
    }
  }
  else
  {
    changes.x = x;
    changes.y = y;
    changes.width = width;
    changes.height = height;

    XConfigureWindow (x11->display, x11->win_video,
                      CWX | CWY | CWWidth | CWHeight, &changes);
  }

  XSync (x11->display, False);
  pthread_mutex_unlock (&x11->mutex_display);

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "window resized");
}

void
x11_map (player_t *player)
{
  x11_t *x11 = NULL;

  if (!player || !player->x11)
    return;

  x11 = player->x11;

  if (!x11->display)
    return;

  x11_resize (player);

  pthread_mutex_lock (&x11->mutex_display);

  if (x11->use_subwin)
  {
    screeninfo_t *screeninfo = x11_get_screeninfo (player);
    if (screeninfo && screeninfo->win_black)
      XMapRaised (x11->display, screeninfo->win_black);
  }
  else
    XMapRaised (x11->display, x11->win_video);

  XSync (x11->display, False);
  pthread_mutex_unlock (&x11->mutex_display);

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "window mapped");
}

void
x11_unmap (player_t *player)
{
  x11_t *x11 = NULL;

  if (!player || !player->x11)
    return;

  x11 = player->x11;

  if (!x11->display)
    return;

  pthread_mutex_lock (&x11->mutex_display);

  if (x11->use_subwin)
  {
    screeninfo_t *screeninfo = x11_get_screeninfo (player);
    if (screeninfo && screeninfo->win_black)
      XUnmapWindow (x11->display, screeninfo->win_black);
  }
  else
    XUnmapWindow (x11->display, x11->win_video);

  XSync (x11->display, False);
  pthread_mutex_unlock (&x11->mutex_display);

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "window unmapped");
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

  pthread_mutex_lock (&x11->mutex_display);
  XUnmapWindow (x11->display, x11->win_video);
  XDestroyWindow (x11->display, x11->win_video);

  if (x11->use_subwin)
  {
    screeninfo_t *screeninfo = x11_get_screeninfo (player);
    if (screeninfo)
    {
      if (screeninfo->win_trans)
      {
        XUnmapWindow (x11->display, screeninfo->win_trans);
        XDestroyWindow (x11->display, screeninfo->win_trans);
      }
      if (screeninfo->win_black)
      {
        XUnmapWindow (x11->display, screeninfo->win_black);
        XDestroyWindow (x11->display, screeninfo->win_black);
      }
      free (screeninfo);
    }
  }

  pthread_mutex_unlock (&x11->mutex_display);
  XCloseDisplay (x11->display);

  pthread_mutex_destroy (&x11->mutex_display);
  free (x11);
  player->x11 = NULL;

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

/*
 * This X11 initialization seems to not work very well with Compiz Window
 * Manager and maybe all related managers. The main problem seems to be
 * the override_redirect attribute. But it works fine when the main window
 * is attached to an other (see player_init(), winid parameter).
 */
int
x11_init (player_t *player)
{
  x11_t *x11 = NULL;
#ifdef HAVE_XINE
  x11_visual_t *vis = NULL;
#endif /* HAVE_XINE */
  Window win_root;
  screeninfo_t *screeninfo;
  int screen, width, height;
  Visual *visual;
  Atom XA_NO_BORDER;
  MWMHints mwmhints;
  XSetWindowAttributes atts;

  if (!player)
    return 0;

  player->x11 = calloc (1, sizeof (x11_t));
  x11 = player->x11;
  if (!x11)
    return 0;

  screeninfo = calloc (1, sizeof (screeninfo_t));
  if (!screeninfo)
    goto err_screeninfo;

  x11->display = XOpenDisplay (NULL);
  if (!x11->display)
  {
    plog (player, PLAYER_MSG_WARNING, MODULE_NAME, "Failed to open display");
    goto err_display;
  }

  if (player->type == PLAYER_TYPE_MPLAYER)
    x11->use_subwin = 1;

  pthread_mutex_init (&x11->mutex_display, NULL);
  screen = XDefaultScreen (x11->display);

  pthread_mutex_lock (&x11->mutex_display);

  win_root = (Window) player->winid;
  if (!win_root)
  {
    /* the video will be in fullscreen */
    width = XDisplayWidth (x11->display, screen);
    height = XDisplayHeight (x11->display, screen);
    visual = XDefaultVisual (x11->display, screen);
    win_root = XRootWindow (x11->display, screen);
  }
  else
  {
    XWindowAttributes atts;
    XGetWindowAttributes (x11->display, win_root, &atts);
    width = atts.width;
    height = atts.height;
    visual = atts.visual;
  }

  atts.override_redirect = True; /* window ignored by the window manager */
  atts.background_pixel = XBlackPixel (x11->display, screen); /* black background */

  /* remove borders */
  XA_NO_BORDER = XInternAtom (x11->display, "_MOTIF_WM_HINTS", False);
  mwmhints.flags = MWM_HINTS_DECORATIONS;
  mwmhints.decorations = 0;

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
    screeninfo->win_black = XCreateWindow (x11->display,
                                           win_root,
                                           0, 0, width, height,
                                           0, 0,
                                           InputOutput,
                                           visual,
                                           CWOverrideRedirect | CWBackPixel, &atts);

    XChangeProperty (x11->display,
                     screeninfo->win_black,
                     XA_NO_BORDER, XA_NO_BORDER, 32,
                     PropModeReplace,
                     (unsigned char *) &mwmhints,
                     PROP_MWM_HINTS_ELEMENTS);

    /* create a window for the video out */
    x11->win_video = XCreateWindow (x11->display,
                                    screeninfo->win_black,
                                    0, 0, width, height,
                                    0, 0,
                                    InputOutput,
                                    visual,
                                    CWOverrideRedirect | CWBackPixel, &atts);

    XMapWindow (x11->display, x11->win_video);

  /*
   * Transparent window to catch all events in order to prevent sending
   * events to MPlayer.
   */
    screeninfo->win_trans = XCreateWindow (x11->display,
                                           screeninfo->win_black,
                                           0, 0, width, height,
                                           0, 0,
                                           InputOnly,
                                           visual,
                                           CWOverrideRedirect, &atts);
    XMapRaised (x11->display, screeninfo->win_trans);
  }
  else
  {
    /* create a window for the video out */
    x11->win_video = XCreateWindow (x11->display,
                                    win_root,
                                    0, 0, width, height,
                                    0, 0,
                                    InputOutput,
                                    visual,
                                    CWOverrideRedirect | CWBackPixel, &atts);
  }

  XChangeProperty (x11->display,
                   x11->win_video,
                   XA_NO_BORDER, XA_NO_BORDER, 32,
                   PropModeReplace,
                   (unsigned char *) &mwmhints,
                   PROP_MWM_HINTS_ELEMENTS);

  XSync (x11->display, False);
  pthread_mutex_unlock (&x11->mutex_display);

  if (player->type == PLAYER_TYPE_XINE)
  {
#ifdef HAVE_XINE
    vis = malloc (sizeof (x11_visual_t));

    if (vis)
    {
      screeninfo->width = width;
      screeninfo->height = height;
      screeninfo->pixel_aspect = 1.0;

      vis->display = x11->display;
      vis->screen = screen;
      vis->d = x11->win_video;
      vis->dest_size_cb = dest_size_cb;
      vis->frame_output_cb = frame_output_cb;
      vis->user_data = (void *) screeninfo;
    }

    x11->data = (void *) vis;
#endif /* HAVE_XINE */
  }
  else if (x11->use_subwin)
  {
    screeninfo->width = width;
    screeninfo->height = height;
    screeninfo->pixel_aspect = 1.0;

    x11->data = (void *) screeninfo;
  }
  else
  {
    free (screeninfo);
    x11->data = NULL;
  }

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "window initialized");
  return 1;

err_display:
  free (screeninfo);
err_screeninfo:
  free (x11);
  player->x11 = NULL;
  return 0;
}
