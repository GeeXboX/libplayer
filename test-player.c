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

#define _GNU_SOURCE
#include <getopt.h>

#include "player.h"


#define TESTPLAYER_OPTIONS \
  "test-player for libplayer\n" \
  "\n" \
  "Usage: test-player [options ...] [MRLs|files ...]\n" \
  "\n" \
  "Options:\n" \
  " -h --help               this help\n" \
  " -p --player <player>    specify the player (mplayer|xine|vlc|gstreamer)\n" \
  " -a --audio  <audioout>  specify the audio output (alsa|oss|null)\n" \
  " -g --video  <videoout>  specify the video output (x11|sdl:x11|xv|fb)\n" \
  " -v --verbose            increase verbosity\n" \
  "\n" \
  "Default values are dummy player, null video and auto audio output.\n" \
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
  " v : print properties and metadata of the current stream\n" \
  " i : print current time position\n" \
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
  if (e == PLAYER_EVENT_PLAYBACK_FINISHED)
    printf ("PLAYBACK FINISHED\n");

  return 0;
}

static int
getch (void)
{
  struct termios oldt, newt;
  int ch = 0;

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
  int c;
  mrl_t *mrl;

  printf ("What resource to load?\n");
  printf (" 1 - Local file\n");
  printf (" 2 - Compact Disc Digital Audio\n");
  c = getch ();

  switch (c) {
  default:
    return;

  case '1':
  {
    char file[1024] = "";
    mrl_resource_local_args_t *args;

    printf ("Media to load (file): ");
    while (!*file) {
      fgets (file, sizeof (file), stdin);
      *(file + strlen (file) - 1) = '\0';
    }

    args = calloc (1, sizeof (mrl_resource_local_args_t));
    if (!args)
      return;

    args->location = strdup (file);

    mrl = mrl_new (player, MRL_RESOURCE_FILE, args);
    if (!mrl) {
      if (args->location)
        free (args->location);
      free (args);
      return;
    }
    break;
  }

  case '2':
  {
    int val;
    char device[256] = "";
    mrl_resource_cd_args_t *args;

    args = calloc (1, sizeof (mrl_resource_cd_args_t));
    if (!args)
      return;

    printf ("Device: ");
    while (!*device) {
      fgets (device, sizeof (device), stdin);
      *(device + strlen (device) - 1) = '\0';
    }
    args->device = strdup (device);

    printf ("Track start: ");
    scanf ("%3u", &val);
    args->track_start = (uint8_t) val;

    printf ("Track end: ");
    scanf ("%3u", &val);
    args->track_end = (uint8_t) val;

    printf ("Speed: ");
    scanf ("%3u", &val);
    args->speed = (uint8_t) val;

    mrl = mrl_new (player, MRL_RESOURCE_CDDA, args);
    if (!mrl) {
      if (args->device)
        free (args->device);
      free (args);
      return;
    }
    break;
  }
  }

  player_mrl_append (player, mrl, PLAYER_MRL_ADD_QUEUE);
  printf ("\nMedia added to the playlist!\n");
}

static void
show_type (player_t *player, mrl_t *mrl)
{
  printf (" Type: ");

  if (!mrl)
    printf ("unknown\n");

  switch (mrl_get_type (player, mrl)) {
  case MRL_TYPE_AUDIO:
    printf ("audio\n");
    break;

  case MRL_TYPE_VIDEO:
    printf ("video\n");
    break;

  case MRL_TYPE_IMAGE:
    printf ("image\n");
    break;

  default:
    printf ("unknown\n");
  }
}

static void
show_resource (player_t *player, mrl_t *mrl)
{
  const char const *resource_desc[] = {
    [MRL_RESOURCE_UNKNOWN] = "unknown",
    [MRL_RESOURCE_CDDA]    = "Compact Disc Digital Audio",
    [MRL_RESOURCE_CDDB]    = "Compact Disc Database",
    [MRL_RESOURCE_DVB]     = "Digital Video Broadcasting",
    [MRL_RESOURCE_DVD]     = "Digital Versatile Disc",
    [MRL_RESOURCE_DVDNAV]  = "Digital Versatile Disc with menu navigation",
    [MRL_RESOURCE_FIFO]    = "FIFO",
    [MRL_RESOURCE_FILE]    = "file",
    [MRL_RESOURCE_FTP]     = "File Transfer Protocol",
    [MRL_RESOURCE_HTTP]    = "Hypertext Transfer Protocol",
    [MRL_RESOURCE_MMS]     = "Microsoft Media Services",
    [MRL_RESOURCE_RADIO]   = "radio analog",
    [MRL_RESOURCE_RTP]     = "Real-time Transport Protocol",
    [MRL_RESOURCE_RTSP]    = "Real Time Streaming Protocol",
    [MRL_RESOURCE_SMB]     = "Samba",
    [MRL_RESOURCE_STDIN]   = "standard input",
    [MRL_RESOURCE_TCP]     = "Transmission Control Protocol",
    [MRL_RESOURCE_TV]      = "Television analog",
    [MRL_RESOURCE_UDP]     = "User Datagram Protocol",
    [MRL_RESOURCE_VCD]     = "Video Compact Disc",
  };
  const int resource_size = sizeof (resource_desc) / sizeof (resource_desc[0]);
  mrl_resource_t resource = mrl_get_resource (player, mrl);

  if (resource > resource_size || resource < 0)
    resource = MRL_RESOURCE_UNKNOWN;

  printf (" Resource: %s\n", resource_desc[resource]);
}

static void
show_info (player_t *player, mrl_t *mrl)
{
  char *meta;
  char *codec;
  uint32_t prop;
  off_t size;

  if (!player || !mrl)
    return;

  printf ("Properties and metadata:\n");

  show_type (player, mrl);
  show_resource (player, mrl);

  size = mrl_get_size (player, mrl);
  printf (" Size: %.2f MB\n", size / 1024.0 / 1024.0);
  prop = mrl_get_property (player, mrl, MRL_PROPERTY_SEEKABLE);
  printf (" Seekable: %i\n", prop);
  prop = mrl_get_property (player, mrl, MRL_PROPERTY_LENGTH);
  printf (" Length: %.2f sec\n", (float) prop / 1000.0);

  codec = mrl_get_video_codec (player, mrl);
  if (codec) {
    printf (" Video Codec: %s\n", codec);
    free (codec);
  }

  prop = mrl_get_property (player, mrl, MRL_PROPERTY_VIDEO_BITRATE);
  if (prop)
    printf (" Video Bitrate: %i kbps\n", prop / 1000);

  prop = mrl_get_property (player, mrl, MRL_PROPERTY_VIDEO_WIDTH);
  if (prop)
    printf (" Video Width: %i\n", prop);

  prop = mrl_get_property (player, mrl, MRL_PROPERTY_VIDEO_HEIGHT);
  if (prop)
    printf (" Video Height: %i\n", prop);

  prop = mrl_get_property (player, mrl, MRL_PROPERTY_VIDEO_ASPECT);
  if (prop)
    printf (" Video Aspect: %.2f\n", prop / 10000.0);

  prop = mrl_get_property (player, mrl, MRL_PROPERTY_VIDEO_CHANNELS);
  if (prop)
    printf (" Video Channels: %i\n", prop);

  prop = mrl_get_property (player, mrl, MRL_PROPERTY_VIDEO_STREAMS);
  if (prop)
    printf (" Video Streams: %i\n", prop);

  prop = mrl_get_property (player, mrl, MRL_PROPERTY_VIDEO_FRAMEDURATION);
  if (prop)
    printf (" Video Framerate: %.2f\n", 90000.0 / prop);

  codec = mrl_get_audio_codec (player, mrl);
  if (codec) {
    printf (" Audio Codec: %s\n", codec);
    free (codec);
  }

  prop = mrl_get_property (player, mrl, MRL_PROPERTY_AUDIO_BITRATE);
  if (prop)
    printf (" Audio Bitrate: %i kbps\n", prop / 1000);

  prop = mrl_get_property (player, mrl, MRL_PROPERTY_AUDIO_BITS);
  if (prop)
    printf (" Audio Bits: %i bps\n", prop);

  prop = mrl_get_property (player, mrl, MRL_PROPERTY_AUDIO_CHANNELS);
  if (prop)
    printf (" Audio Channels: %i\n", prop);

  prop = mrl_get_property (player, mrl, MRL_PROPERTY_AUDIO_SAMPLERATE);
  if (prop)
    printf (" Audio Sample Rate: %i Hz\n", prop);

  meta = mrl_get_metadata (player, mrl, MRL_METADATA_TITLE);
  if (meta) {
    printf (" Meta Title: %s\n", meta);
    free (meta);
  }

  meta = mrl_get_metadata (player, mrl, MRL_METADATA_ARTIST);
  if (meta) {
    printf (" Meta Artist: %s\n", meta);
    free (meta);
  }

  meta = mrl_get_metadata (player, mrl, MRL_METADATA_GENRE);
  if (meta) {
    printf (" Meta Genre: %s\n", meta);
    free (meta);
  }

  meta = mrl_get_metadata (player, mrl, MRL_METADATA_ALBUM);
  if (meta) {
    printf (" Meta Album: %s\n", meta);
    free (meta);
  }

  meta = mrl_get_metadata (player, mrl, MRL_METADATA_YEAR);
  if (meta) {
    printf (" Meta Year: %s\n", meta);
    free (meta);
  }

  meta = mrl_get_metadata (player, mrl, MRL_METADATA_TRACK);
  if (meta) {
    printf (" Meta Track: %s\n", meta);
    free (meta);
  }

  meta = mrl_get_metadata (player, mrl, MRL_METADATA_COMMENT);
  if (meta) {
    printf (" Meta Comment: %s\n", meta);
    free (meta);
  }
}

int
main (int argc, char **argv)
{
  player_t *player;
  player_type_t type = PLAYER_TYPE_DUMMY;
  player_vo_t vo = PLAYER_VO_NULL;
  player_ao_t ao = PLAYER_AO_AUTO;
  char input;
  int run = 1;
  int volume = 85;
  int time_pos;
  player_verbosity_level_t verbosity = PLAYER_MSG_ERROR;

  int c, index;
  const char *const short_options = "hvp:a:g:";
  const struct option long_options [] = {
    {"help",    no_argument,       0, 'h' },
    {"verbose", no_argument,       0, 'v' },
    {"player",  required_argument, 0, 'p' },
    {"audio",   required_argument, 0, 'a' },
    {"video",   required_argument, 0, 'g' },
    {0,         0,                 0,  0  }
  };

  /* command line argument processing */
  while (1) {
    c = getopt_long (argc, argv, short_options, long_options, &index);

    if (c == EOF)
      break;

    switch (c) {
    case 0:
      /* opt = long_options[index].name; */
      break;

    case '?':
    case 'h':
      printf (TESTPLAYER_HELP);
      return 0;

    case 'v':
      if (verbosity == PLAYER_MSG_ERROR)
        verbosity = PLAYER_MSG_WARNING;
      else
        verbosity = PLAYER_MSG_INFO;
      break;

    case 'p':
      if (!strcmp (optarg, "mplayer"))
#ifdef HAVE_MPLAYER
        type = PLAYER_TYPE_MPLAYER;
#else
        printf ("MPlayer not supported, dummy player used instead!\n");
#endif /* HAVE_MPLAYER */
      if (!strcmp (optarg, "xine"))
#ifdef HAVE_XINE
        type = PLAYER_TYPE_XINE;
#else
        printf ("Xine not supported, dummy player used instead!\n");
#endif /* HAVE_XINE */
      if (!strcmp (optarg, "vlc"))
#ifdef HAVE_VLC
        type = PLAYER_TYPE_VLC;
#else
        printf ("VLC not supported, dummy player used instead!\n");
#endif /* HAVE_VLC */
      if (!strcmp (optarg, "gstreamer"))
#ifdef HAVE_GSTREAMER
        type = PLAYER_TYPE_GSTREAMER;
#else
        printf ("GStreamer not supported, dummy player used instead!\n");
#endif /* HAVE_GSTREAMER */
      break;

    case 'a':
      if (!strcmp (optarg, "alsa"))
        ao = PLAYER_AO_ALSA;
      else if (!strcmp (optarg, "oss"))
        ao = PLAYER_AO_OSS;
      else if (!strcmp (optarg, "null"))
        ao = PLAYER_AO_NULL;
      break;

    case 'g':
      if (!strcmp (optarg, "x11"))
        vo = PLAYER_VO_X11;
      else if (!strcmp (optarg, "sdl:x11"))
        vo = PLAYER_VO_X11_SDL;
      else if (!strcmp (optarg, "xv"))
        vo = PLAYER_VO_XV;
      else if (!strcmp (optarg, "fb"))
        vo = PLAYER_VO_FB;
      break;

    default:
      printf (TESTPLAYER_HELP);
      return -1;
    }
  }

  player = player_init (type, ao, vo, verbosity, event_cb);

  if (!player)
    return -1;

  /* these arguments are MRLs|files */
  if (optind < argc) {
    do {
      mrl_t *mrl;
      mrl_resource_local_args_t *args;

      args = calloc (1, sizeof (mrl_resource_local_args_t));
      args->location = strdup (argv[optind]);

      mrl = mrl_new (player, MRL_RESOURCE_FILE, args);
      if (!mrl)
        continue;
      printf (" > %s added to the playlist!\n", argv[optind]);
      player_mrl_append (player, mrl, PLAYER_MRL_ADD_QUEUE);
    } while (++optind < argc);
    putchar ('\n');
  }

  player_audio_volume_set (player, volume);
  printf (TESTPLAYER_COMMANDS);

  /* main loop */
  while (run) {
    printf ("action> ");
    fflush (stdout);
    input = getch ();

    switch (input) {
    case '0':   /* increase volume */
      if (++volume > 100)
        volume = 100;
      player_audio_volume_set (player, volume);
      printf ("VOLUME %i\n", volume);
      break;
    case '1':   /* 5s backward */
      player_playback_seek (player, -5, PLAYER_PB_SEEK_RELATIVE);
      printf ("SEEK -5 sec.\n");
      break;
    case '2':   /* 5s forward */
      player_playback_seek (player, 5, PLAYER_PB_SEEK_RELATIVE);
      printf ("SEEK +5 sec.\n");
      break;
    case '9':   /* decrease volume */
      if (--volume < 0)
        volume = 0;
      player_audio_volume_set (player, volume);
      printf ("VOLUME %i\n", volume);
      break;
    case 'b':   /* start the previous stream in the playlist */
      player_mrl_previous (player);
      printf ("PREVIOUS STREAM\n");
      break;
    case 'i':   /* print current time position */
      time_pos = player_get_time_pos (player);
      printf ("TIME POSITION: %.2f sec\n",
              time_pos < 0 ? 0.0 : (float) time_pos / 1000.0);
      break;
    case 'l':   /* load a stream in the playlist */
      load_media (player);
      break;
    case 'm':   /* set/unset mute */
      if (player_audio_mute_get (player) != PLAYER_MUTE_ON) {
        player_audio_mute_set (player, PLAYER_MUTE_ON);
        printf ("MUTE\n");
      }
      else {
        player_audio_mute_set (player, PLAYER_MUTE_OFF);
        printf ("UNMUTE\n");
      }
      break;
    case 'n':   /* start the next stream in the playlist */
      player_mrl_next (player);
      printf ("NEXT STREAM\n");
      break;
    case 'o':   /* pause the current playback */
      player_playback_pause (player);
      printf ("PAUSE\n");
      break;
    case 'p':   /* start a new playback */
      player_playback_start (player);
      printf ("START PLAYBACK\n");
      break;
    case 'q':   /* quit test-player */
      run = 0;
      printf ("QUIT\n");
      break;
    case 'r':   /* remove the current stream of the playlist */
      player_mrl_remove (player);
      printf ("REMOVE STREAM OF THE PLAYLIST\n");
      break;
    case 's':   /* stop the current playback */
      player_playback_stop (player);
      printf ("STOP PLAYBACK\n");
      break;
    case 't':   /* remove all streams of the playlist */
      player_mrl_remove_all (player);
      printf ("ERASE PLAYLIST\n");
      break;
    case 'v':   /* print properties and metadata */
      show_info (player, player_mrl_get_current (player));
      break;
    default:
      fprintf (stderr, "ERROR: Command unknown!\n");
      printf (TESTPLAYER_COMMANDS);
    }
  }

  player_uninit (player);

  return 0;
}
