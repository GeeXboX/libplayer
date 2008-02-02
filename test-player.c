/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2008 Mathieu Schroeter <mathieu.schroeter@gamesover.ch>
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
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

#include "player.h"


#define TESTPLAYER_OPTIONS \
  "test-player for libplayer\n" \
  "\n" \
  "Usage: test-player [-h|options]\n" \
  "\n" \
  " -h                this help\n" \
  "\n" \
  "Options:\n" \
  " -p <player>       specify the player (mplayer|xine|vlc|gstreamer)\n" \
  " -ao <audio_out>   specify the audio output (alsa|oss)\n" \
  " -vo <video_out>   specify the video output (x11|sdl:x11|xv|fb)\n" \
  "\n" \
  "Default values are dummy player, null video and audio output.\n" \
  "\n"
#define TESTPLAYER_COMMANDS \
  "Commands for use test-player:\n" \
  "\n" \
  " 0 : increase volume\n" \
  " 9 : decrease volume\n" \
  " m : set/unset mute\n" \
  " 1 : 5s backward\n" \
  " 2 : 5s forward\n" \
  " l : load a stream in the playlist\n" \
  " p : start a new playback\n" \
  " o : pause the current playback\n" \
  " s : stop the current playback\n" \
  " b : start the previous stream in the playlist\n" \
  " n : start the next stream in the playlist\n" \
  " r : remove the current stream of the playlist\n" \
  " t : remove all streams of the playlist\n" \
  " q : quit test-player\n" \
  "\n"
#define TESTPLAYER_HELP TESTPLAYER_OPTIONS TESTPLAYER_COMMANDS

static int
event_cb (player_event_t e, void *data)
{
  return 0;
}

static int
getch (void)
{
  struct termios oldt, newt;
  int ch;

  tcgetattr (STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr (STDIN_FILENO, TCSANOW, &newt);

  read (STDIN_FILENO, &ch, 1);
  putchar (ch);
  putchar ('\n');

  tcsetattr (STDIN_FILENO, TCSANOW, &oldt);

  return ch;
}

static void
load_media (player_t *player)
{
  player_mrl_type_t type = PLAYER_MRL_TYPE_UNKNOWN;
  char file[1024];
  char media;

  printf ("Media to load (MRL|file): ");
  scanf ("%s", file);
  putchar ('\n');

  printf ("Media type :\n" \
          " 1. Audio file\n" \
          " 2. Video file\n" \
          " 3. Image file (unimplemented)\n" \
          " 4. DVD\n" \
          " 5. DVD navigation\n" \
          " 6. CD Audio\n");
  media = getch ();

  switch (media) {
  case '1':
    type = PLAYER_MRL_TYPE_FILE_AUDIO;
    break;
  case '2':
    type = PLAYER_MRL_TYPE_FILE_VIDEO;
    break;
  case '3':
    type = PLAYER_MRL_TYPE_FILE_IMAGE;
    break;
  case '4':
    type = PLAYER_MRL_TYPE_DVD_SIMPLE;
    break;
  case '5':
    type = PLAYER_MRL_TYPE_DVD_NAV;
    break;
  case '6':
    type = PLAYER_MRL_TYPE_CDDA;
    break;
  default:
    fprintf (stderr, "ERROR: Media type unknown!\n");
    return;
  }

  player_mrl_append (player, file, NULL, type, PLAYER_ADD_MRL_QUEUE);
  printf ("Media added to the playlist!\n");
}

int
main (int argc, char **argv)
{
  player_t *player;
  player_type_t type = PLAYER_TYPE_DUMMY;
  player_vo_t vo = PLAYER_VO_NULL;
  player_ao_t ao = PLAYER_AO_NULL;
  char input;
  int run = 1;
  int volume = 85;

  if (argc > 1 && !strcmp (argv[1], "-h")) {
    printf (TESTPLAYER_HELP);
    return 0;
  }

  while ((argc -= 2) > 0) {
    if (!strcmp (argv[argc], "-p")) {
      if (!strcmp (argv[argc + 1], "mplayer"))
#ifdef HAVE_MPLAYER
        type = PLAYER_TYPE_MPLAYER;
#else
        printf ("MPlayer not supported, dummy player used instead!\n");
#endif /* HAVE_MPLAYER */
      if (!strcmp (argv[argc + 1], "xine"))
#ifdef HAVE_XINE
        type = PLAYER_TYPE_XINE;
#else
        printf ("Xine not supported, dummy player used instead!\n");
#endif /* HAVE_XINE */
      if (!strcmp (argv[argc + 1], "vlc"))
#ifdef HAVE_VLC
        type = PLAYER_TYPE_VLC;
#else
        printf ("VLC not supported, dummy player used instead!\n");
#endif /* HAVE_VLC */
      if (!strcmp (argv[argc + 1], "gstreamer"))
#ifdef HAVE_GSTREAMER
        type = PLAYER_TYPE_GSTREAMER;
#else
        printf ("GStreamer not supported, dummy player used instead!\n");
#endif /* HAVE_GSTREAMER */
    }
    else if (!strcmp (argv[argc], "-ao")) {
      if (!strcmp (argv[argc + 1], "alsa"))
        ao = PLAYER_AO_ALSA;
      else if (!strcmp (argv[argc + 1], "oss"))
        ao = PLAYER_AO_OSS;
    }
    else if (!strcmp (argv[argc], "-vo")) {
      if (!strcmp (argv[argc + 1], "x11"))
        vo = PLAYER_VO_X11;
      else if (!strcmp (argv[argc + 1], "sdl:x11"))
        vo = PLAYER_VO_X11_SDL;
      else if (!strcmp (argv[argc + 1], "xv"))
        vo = PLAYER_VO_XV;
      else if (!strcmp (argv[argc + 1], "fb"))
        vo = PLAYER_VO_FB;
    }
  }

  player = player_init (type, ao, vo, event_cb);

  if (!player)
    return -1;

  player_set_volume (player, volume);
  printf (TESTPLAYER_COMMANDS);

  /* main loop */
  while (run) {
    input = getch ();

    switch (input) {
    case '0':   /* increase volume */
      if (++volume > 100)
        volume = 100;
      player_set_volume (player, volume);
      break;
    case '1':   /* 5s backward */
      player_playback_seek (player, -5);
      break;
    case '2':   /* 5s forward */
      player_playback_seek (player, 5);
      break;
    case '9':   /* decrease volume */
      if (--volume < 0)
        volume = 0;
      player_set_volume (player, volume);
      break;
    case 'b':   /* start the previous stream in the playlist */
      player_mrl_previous (player);
      break;
    case 'l':   /* load a stream in the playlist */
      load_media (player);
      break;
    case 'm':   /* set/unset mute */
      if (player_get_mute (player) != PLAYER_MUTE_ON)
        player_set_mute (player, PLAYER_MUTE_ON);
      else
        player_set_mute (player, PLAYER_MUTE_OFF);
      break;
    case 'n':   /* start the next stream in the playlist */
      player_mrl_next (player);
      break;
    case 'o':   /* pause the current playback */
      player_playback_pause (player);
      break;
    case 'p':   /* start a new playback */
      player_playback_start (player);
      break;
    case 'q':   /* quit test-player */
      run = 0;
      break;
    case 'r':   /* remove the current stream of the playlist */
      player_mrl_remove (player);
      break;
    case 's':   /* stop the current playback */
      player_playback_stop (player);
      break;
    case 't':   /* remove all streams of the playlist */
      player_mrl_remove (player);
      break;
    default:
      fprintf (stderr, "ERROR: Command unknown!\n");
      printf (TESTPLAYER_COMMANDS);
    }
  }

  player_uninit (player);

  return 0;
}
