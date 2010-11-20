/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2008-2009 Davide Cavalca <davide AT geexbox.org>
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

#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>

#if defined (HAVE_WIN_XCB) && defined (USE_XLIB_HACK)
#include <X11/Xlib.h>
#endif /* HAVE_WIN_XCB && USE_XLIB_HACK */

#include "player.h"

static uint32_t
getch (void)
{
  struct termios oldt, newt;
  uint32_t ch = 0;
  uint32_t val = 0;
  int n;

  tcgetattr (STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr (STDIN_FILENO, TCSANOW, &newt);

  n = read (STDIN_FILENO, &ch, sizeof (ch));
  putchar ('\n');
  while (ch)
  {
    val <<= 8;
    val += ch & 0xFF;
    ch >>= 8;
  }

  tcsetattr (STDIN_FILENO, TCSANOW, &oldt);

  return val;
}

static int
event_cb (player_event_t e, pl_unused void *data)
{
  printf ("Received event (%i): ", e);

  switch (e)
  {
  case PLAYER_EVENT_UNKNOWN:
    printf ("unknown event\n");
    break;
  case PLAYER_EVENT_PLAYBACK_START:
    printf ("playback started\n");
    break;
  case PLAYER_EVENT_PLAYBACK_STOP:
    printf ("playback stopped\n");
    break;
  case PLAYER_EVENT_PLAYBACK_FINISHED:
    printf ("playback finished\n");
    break;
  case PLAYER_EVENT_PLAYLIST_FINISHED:
    printf ("playlist finished\n");
    break;
  case PLAYER_EVENT_PLAYBACK_PAUSE:
    printf ("playback paused\n");
    break;
  case PLAYER_EVENT_PLAYBACK_UNPAUSE:
    printf ("playback unpaused\n");
    break;
  }

  return 0;
}

int
main (pl_unused int argc, pl_unused char **argv)
{
  player_t *player;
  player_init_param_t param;
  player_type_t type = PLAYER_TYPE_XINE;
  player_verbosity_level_t verbosity = PLAYER_MSG_INFO;
  mrl_t *mrl = NULL;
  mrl_resource_tv_args_t *args;
  uint32_t input;

#if defined (HAVE_WIN_XCB) && defined (USE_XLIB_HACK)
  XInitThreads ();
#endif /* HAVE_WIN_XCB && USE_XLIB_HACK */

  memset (&param, 0, sizeof (player_init_param_t));
  param.ao       = PLAYER_AO_ALSA;
  param.vo       = PLAYER_VO_X11;
  param.event_cb = event_cb;

  player = player_init (type, verbosity, &param);

  args = calloc (1, sizeof (mrl_resource_tv_args_t));
  args->device = strdup ("/tmp/vdr-xine/stream");
  args->driver = strdup ("demux:mpeg_pes");
  mrl = mrl_new (player, MRL_RESOURCE_VDR, args);
  player_mrl_set (player, mrl);
  player_playback_start (player);

  for (;;)
  {
    input = getch ();
    switch (input)
    {
    case 0x1B5B41: /* UP */
      player_vdr (player, PLAYER_VDR_UP);
      printf ("UP\n");
      break;
    case 0x1B5B42: /* DOWN */
      player_vdr (player, PLAYER_VDR_DOWN);
      printf ("DOWN\n");
      break;
    case 0x1B5B44: /* LEFT */
      player_vdr (player, PLAYER_VDR_LEFT);
      printf ("LEFT\n");
      break;
    case 0x1B5B43: /* RIGHT */
      player_vdr (player, PLAYER_VDR_RIGHT);
      printf ("RIGHT\n");
      break;
    case 0xA: /* ENTER */
      player_vdr (player, PLAYER_VDR_OK);
      printf ("OK\n");
      break;
    case 0x20: /* SPACE */
      player_vdr (player, PLAYER_VDR_MENU);
      printf ("MENU\n");
      break;
    case 0x7F: /* BACKSPACE */
      player_vdr (player, PLAYER_VDR_BACK);
      printf ("BACK\n");
      break;
    case 0x30: /* 0 */
      player_vdr (player, PLAYER_VDR_0);
      printf ("0\n");
      break;
    case 0x31: /* 1 */
      player_vdr (player, PLAYER_VDR_1);
      printf ("1\n");
      break;
    case 0x32: /* 2 */
      player_vdr (player, PLAYER_VDR_2);
      printf ("1\n");
      break;
    case 0x33: /* 3 */
      player_vdr (player, PLAYER_VDR_3);
      printf ("1\n");
      break;
    case 0x34: /* 4 */
      player_vdr (player, PLAYER_VDR_4);
      printf ("1\n");
      break;
    case 0x35: /* 5 */
      player_vdr (player, PLAYER_VDR_5);
      printf ("1\n");
      break;
    case 0x36: /* 6 */
      player_vdr (player, PLAYER_VDR_6);
      printf ("1\n");
      break;
    case 0x37: /* 7 */
      player_vdr (player, PLAYER_VDR_7);
      printf ("1\n");
      break;
    case 0x38: /* 8 */
      player_vdr (player, PLAYER_VDR_8);
      printf ("1\n");
      break;
    case 0x39: /* 9 */
      player_vdr (player, PLAYER_VDR_9);
      printf ("1\n");
      break;
    case 0x71: /* Q */
      player_vdr (player, PLAYER_VDR_RED);
      printf ("Q\n");
      break;
    case 0x77: /* W */
      player_vdr (player, PLAYER_VDR_GREEN);
      printf ("W\n");
      break;
    case 0x65: /* E */
      player_vdr (player, PLAYER_VDR_YELLOW);
      printf ("E\n");
      break;
    case 0x72: /* R */
      player_vdr (player, PLAYER_VDR_BLUE);
      printf ("R\n");
      break;
    case 0x1B: /* ESC */
      printf ("QUIT\n");
      player_playback_stop (player);
      player_uninit (player);
      return EXIT_SUCCESS;
    default:
      fprintf (stderr, "ERROR: Command unknown %x\n", input);
    }
  }
}
