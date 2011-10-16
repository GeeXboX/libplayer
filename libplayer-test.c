/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2008-2009 Mathieu Schroeter <mathieu@schroetersa.ch>
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
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <inttypes.h>

#if defined (HAVE_WIN_XCB) && defined (USE_XLIB_HACK)
#include <X11/Xlib.h>
#endif /* HAVE_WIN_XCB && USE_XLIB_HACK */

#define _GNU_SOURCE
#include <getopt.h>

#include "player.h"

#define APPNAME "libplayer-test"

#define TESTPLAYER_OPTIONS \
  APPNAME " for libplayer\n" \
  "\n" \
  "Usage: " APPNAME " [options ...] [files ...]\n" \
  "\n" \
  "Options:\n" \
  " -h --help               this help\n" \
  " -p --player <player>    specify the player (mplayer|xine|vlc|gstreamer)\n" \
  " -a --audio  <audioout>  specify the audio output (alsa|oss|pulse|null)\n" \
  " -g --video  <videoout>  specify the video output (x11|sdl:x11|xv|gl|vdpau|fb|directfb|vaapi|v4l2|null)\n" \
  " -q --quality <level>    specify the picture quality (0|1|2, best to worse)\n" \
  " -v --verbose            increase verbosity\n" \
  "\n" \
  "Default values are dummy player, auto video and auto audio output.\n" \
  "\n"
#define TESTPLAYER_COMMANDS \
  "Commands to use " APPNAME ":\n" \
  "\n" \
  " #   : change playback mode (auto or single)\n" \
  " .   : change loop value and mode\n" \
  " ,   : enable/disable shuffle on the playlist\n" \
  " %%   : write a text on the OSD\n" \
  " k   : enable/disable OSD\n" \
  " +/- : increase/decrease speed\n" \
  " ]/[ : audio delay +/- 100 ms\n" \
  " 0/9 : increase/decrease volume\n" \
  " m   : set/unset mute\n" \
  " 2/1 : 5s forward/backward\n" \
  " 3/4 : previous/next audio track\n" \
  " 5/6 : previous/next subtitle\n" \
  " 7/8 : previous/next TV analog channel\n" \
  " {/} : previous/next radio channel\n" \
  " (/) : previous/next chapter\n" \
  " u   : toggle subtitle visibility\n" \
  " a   : change aspect ratio (original/16:9)\n" \
  " l   : load a stream in the playlist\n" \
  " v   : print properties and metadata of the current stream\n" \
  " j   : take a video snapshot of a specific time position\n" \
  " y   : select a radio channel\n" \
  " z   : select a TV channel\n" \
  " i   : print current time position\n" \
  " p   : start a new playback\n" \
  " o   : pause/unpause the current playback\n" \
  " s   : stop the current playback\n" \
  " b   : start the previous stream in the playlist\n" \
  " n   : start the next stream in the playlist\n" \
  " c   : continue with the next stream accordingly to the playback mode.\n" \
  " r   : remove the current stream of the playlist\n" \
  " t   : remove all streams of the playlist\n" \
  " q   : quit " APPNAME "\n" \
  "\n" \
  "Commands for dvdnav:\n" \
  "\n" \
  " ARROWS    : menu navigation\n" \
  " BACKSPACE : return to menu\n" \
  " ENTER     : select\n" \
  "\n"
#define TESTPLAYER_HELP TESTPLAYER_OPTIONS TESTPLAYER_COMMANDS

static int
event_cb (player_event_t e, pl_unused void *data)
{
  printf ("Received event (%i)\n", e);

  if (e == PLAYER_EVENT_PLAYBACK_FINISHED)
    printf ("PLAYBACK FINISHED\n");

  return 0;
}

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
  if (!n)
    return 0;

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

static mrl_t *
load_res_local (player_t *player)
{
  char file[1024] = "";
  mrl_t *mrl = NULL;
  mrl_resource_local_args_t *args;

  printf ("Media to load (file): ");
  while (!*file)
  {
    char *r;
    r = fgets (file, sizeof (file), stdin);
    if (!r)
      continue;
    *(file + strlen (file) - 1) = '\0';
  }

  args = calloc (1, sizeof (mrl_resource_local_args_t));
  if (!args)
    return NULL;

  args->location = strdup (file);

  mrl = mrl_new (player, MRL_RESOURCE_FILE, args);
  if (!mrl)
  {
    if (args->location)
      free (args->location);
    free (args);
    return NULL;
  }

  return mrl;
}

static mrl_t *
load_res_cd (player_t *player)
{
  int val;
  char device[256] = "";
  mrl_t *mrl = NULL;
  mrl_resource_cd_args_t *args;
  mrl_resource_t res;
  int n;

  args = calloc (1, sizeof (mrl_resource_cd_args_t));
  if (!args)
    return NULL;

  printf ("Device: ");
  while (!*device)
  {
    char *r;
    r = fgets (device, sizeof (device), stdin);
    if (!r)
      continue;
    *(device + strlen (device) - 1) = '\0';
  }
  args->device = strdup (device);

  printf ("cddb [0|1]: ");
  n = scanf ("%1u", &val);
  if (n != 1)
    val = 1;
  res = val ? MRL_RESOURCE_CDDB : MRL_RESOURCE_CDDA;

  printf ("Track start: ");
  n = scanf ("%3u", &val);
  if (n != 1)
    val = 1;
  args->track_start = (uint8_t) val;

  printf ("Track end: ");
  n = scanf ("%3u", &val);
  if (n != 1)
    val = 99;
  args->track_end = (uint8_t) val;

  printf ("Speed: ");
  n = scanf ("%3u", &val);
  if (n != 1)
    val = 1;
  args->speed = (uint8_t) val;

  mrl = mrl_new (player, res, args);
  if (!mrl)
  {
    if (args->device)
      free (args->device);
    free (args);
    return NULL;
  }

  return mrl;
}

static mrl_t *
load_res_dvd (player_t *player)
{
  int val;
  char device[256] = "";
  mrl_t *mrl;
  mrl_resource_videodisc_args_t *args;
  mrl_resource_t res;
  int n;

  args = calloc (1, sizeof (mrl_resource_videodisc_args_t));
  if (!args)
    return NULL;

  printf ("Device: ");
  while (!*device)
  {
    char *r;
    r = fgets (device, sizeof (device), stdin);
    if (!r)
      continue;
    *(device + strlen (device) - 1) = '\0';
  }
  args->device = strdup (device);

  printf ("dvdnav [0|1]: ");
  n = scanf ("%1u", &val);
  if (n != 1)
    val = 0;
  res = val ? MRL_RESOURCE_DVDNAV : MRL_RESOURCE_DVD;

  printf ("Title start: ");
  n = scanf ("%3u", &val);
  if (n != 1)
    val = 1;
  args->title_start = (uint8_t) val;

  printf ("Title end: ");
  n = scanf ("%3u", &val);
  if (n != 1)
    val = 1;
  args->title_end = (uint8_t) val;

  printf ("Angle: ");
  n = scanf ("%3u", &val);
  if (n != 1)
    val = 1;
  args->angle = (uint8_t) val;

  mrl = mrl_new (player, res, args);
  if (!mrl)
  {
    if (args->device)
      free (args->device);
    free (args);
    return NULL;
  }

  return mrl;
}

static mrl_t *
load_res_vcd (player_t *player)
{
  int val;
  char device[256] = "";
  mrl_t *mrl;
  mrl_resource_videodisc_args_t *args;
  mrl_resource_t res = MRL_RESOURCE_VCD;
  int n;

  args = calloc (1, sizeof (mrl_resource_videodisc_args_t));
  if (!args)
    return NULL;

  printf ("Device: ");
  while (!*device)
  {
    char *r;
    r = fgets (device, sizeof (device), stdin);
    if (!r)
      continue;
    *(device + strlen (device) - 1) = '\0';
  }
  args->device = strdup (device);

  printf ("Track start: ");
  n = scanf ("%4u", &val);
  if (n != 1)
    val = 1;
  args->track_start = (uint8_t) val;

  mrl = mrl_new (player, res, args);
  if (!mrl)
  {
    if (args->device)
      free (args->device);
    free (args);
    return NULL;
  }

  return mrl;
}

static mrl_t *
load_res_radio (player_t *player)
{
  char str[256] = "";
  mrl_t *mrl;
  mrl_resource_tv_args_t *args;
  mrl_resource_t res = MRL_RESOURCE_RADIO;

  args = calloc (1, sizeof (mrl_resource_tv_args_t));
  if (!args)
    return NULL;

  printf ("Channel ('null' to disable): ");
  while (!*str)
  {
    char *r;
    r = fgets (str, sizeof (str), stdin);
    if (!r)
      continue;
    *(str + strlen (str) - 1) = '\0';
  }
  if (strcmp (str, "null"))
    args->channel = strdup (str);

  mrl = mrl_new (player, res, args);
  if (!mrl)
  {
    if (args->channel)
      free (args->channel);
    free (args);
    return NULL;
  }

  return mrl;
}

static mrl_t *
load_res_tv (player_t *player)
{
  int val;
  char str[256] = "";
  mrl_t *mrl;
  mrl_resource_tv_args_t *args;
  mrl_resource_t res = MRL_RESOURCE_TV;
  int n;

  args = calloc (1, sizeof (mrl_resource_tv_args_t));
  if (!args)
    return NULL;

  printf ("Channel ('null' to disable): ");
  while (!*str)
  {
    char *r;
    r = fgets (str, sizeof (str), stdin);
    if (!r)
      continue;
    *(str + strlen (str) - 1) = '\0';
  }
  if (strcmp (str, "null"))
    args->channel = strdup (str);
  *str = '\0';

  printf ("Input: ");
  n = scanf ("%u", &val);
  if (n != 1)
    val = 1;
  args->input = (uint8_t) val;

  printf ("Norm (null, PAL, SECAM, NTSC, ...): ");
  while (!*str)
  {
    char *r;
    r = fgets (str, sizeof (str), stdin);
    if (!r)
      continue;
    *(str + strlen (str) - 1) = '\0';
  }
  if (strcmp (str, "null"))
    args->norm = strdup (str);

  mrl = mrl_new (player, res, args);
  if (!mrl)
  {
    if (args->channel)
      free (args->channel);
    if (args->norm)
      free (args->norm);
    free (args);
    return NULL;
  }

  return mrl;
}

static mrl_t *
load_res_network (player_t *player)
{
  char url[4096] = "";
  mrl_t *mrl;
  mrl_resource_network_args_t *args;
  mrl_resource_t res;

  args = calloc (1, sizeof (mrl_resource_network_args_t));
  if (!args)
    return NULL;

  printf ("URL: ");
  while (!*url)
  {
    char *r;
    r = fgets (url, sizeof (url), stdin);
    if (!r)
      continue;
    *(url + strlen (url) - 1) = '\0';
  }
  args->url = strdup (url);

  if (!args->url)
    return NULL;

  if (strstr (url, "http://") == url)
    res = MRL_RESOURCE_HTTP;
  else if (strstr (url, "mms://") == url)
    res = MRL_RESOURCE_MMS;
  else
  {
    free (args->url);
    return NULL;
  }

  mrl = mrl_new (player, res, args);
  if (!mrl)
  {
    free (args->url);
    free (args);
    return NULL;
  }

  return mrl;
}

static void
load_media (player_t *player)
{
  int c;
  mrl_t *mrl = NULL;

  printf ("What resource to load?\n");
  printf (" 1 - Local file\n");
  printf (" 2 - Compact Disc (CDDA/CDDB)\n");
  printf (" 3 - Digital Versatile Disc (Video)\n");
  printf (" 4 - Network stream (HTTP/MMS)\n");
  printf (" 5 - Video Compact Disc (VCD)\n");
  printf (" 6 - Television analog (TV)\n");
  printf (" 7 - Radio analog (RADIO)\n");
  c = getch ();

  switch (c)
  {
  default:
    return;

  case '1':
    mrl = load_res_local (player);
    break;

  case '2':
    mrl = load_res_cd (player);
    break;

  case '3':
    mrl = load_res_dvd (player);
    break;

  case '4':
    mrl = load_res_network (player);
    break;

  case '5':
    mrl = load_res_vcd (player);
    break;

  case '6':
    mrl = load_res_tv (player);
    break;

  case '7':
    mrl = load_res_radio (player);
    break;
  }

  if (!mrl)
    return;

  player_mrl_append (player, mrl, PLAYER_MRL_ADD_QUEUE);
  printf ("\nMedia added to the playlist!\n");
}

static void
show_type (player_t *player, mrl_t *mrl)
{
  printf (" Type: ");

  if (!mrl)
    printf ("unknown\n");

  switch (mrl_get_type (player, mrl))
  {
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
  const unsigned int resource_size =
    sizeof (resource_desc) / sizeof (resource_desc[0]);
  mrl_resource_t resource = mrl_get_resource (player, mrl);

  if (resource > resource_size)
    resource = MRL_RESOURCE_UNKNOWN;

  printf (" Resource: %s\n", resource_desc[resource]);
}

static void
show_info (player_t *player, mrl_t *mrl)
{
  unsigned int i;
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
  if (codec)
  {
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
    printf (" Video Aspect: %.2f\n", prop / PLAYER_VIDEO_ASPECT_RATIO_MULT);

  prop = mrl_get_property (player, mrl, MRL_PROPERTY_VIDEO_CHANNELS);
  if (prop)
    printf (" Video Channels: %i\n", prop);

  prop = mrl_get_property (player, mrl, MRL_PROPERTY_VIDEO_STREAMS);
  if (prop)
    printf (" Video Streams: %i\n", prop);

  prop = mrl_get_property (player, mrl, MRL_PROPERTY_VIDEO_FRAMEDURATION);
  if (prop)
    printf (" Video Framerate: %.2f\n",
            PLAYER_VIDEO_FRAMEDURATION_RATIO_DIV / prop);

  codec = mrl_get_audio_codec (player, mrl);
  if (codec)
  {
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
  if (meta)
  {
    printf (" Meta Title: %s\n", meta);
    free (meta);
  }

  meta = mrl_get_metadata (player, mrl, MRL_METADATA_ARTIST);
  if (meta)
  {
    printf (" Meta Artist: %s\n", meta);
    free (meta);
  }

  meta = mrl_get_metadata (player, mrl, MRL_METADATA_GENRE);
  if (meta)
  {
    printf (" Meta Genre: %s\n", meta);
    free (meta);
  }

  meta = mrl_get_metadata (player, mrl, MRL_METADATA_ALBUM);
  if (meta)
  {
    printf (" Meta Album: %s\n", meta);
    free (meta);
  }

  meta = mrl_get_metadata (player, mrl, MRL_METADATA_YEAR);
  if (meta)
  {
    printf (" Meta Year: %s\n", meta);
    free (meta);
  }

  meta = mrl_get_metadata (player, mrl, MRL_METADATA_TRACK);
  if (meta)
  {
    printf (" Meta Track: %s\n", meta);
    free (meta);
  }

  meta = mrl_get_metadata (player, mrl, MRL_METADATA_COMMENT);
  if (meta)
  {
    printf (" Meta Comment: %s\n", meta);
    free (meta);
  }

  /* Subtitles */

  prop = mrl_get_metadata_subtitle_nb (player, mrl);
  for (i = 1; i <= prop; i++)
  {
    int ret;
    uint32_t id = 0;
    char *name = NULL, *lang = NULL;

    ret = mrl_get_metadata_subtitle (player, mrl, i, &id, &name, &lang);
    if (!ret)
      continue;

    printf (" Meta Subtitle %u", id);
    if (name)
    {
      printf (" Name: %s", name);
      free (name);
    }
    if (lang)
    {
      printf (" (%s)", lang);
      free (lang);
    }
    printf ("\n");
  }

  /* Audio streams */

  prop = mrl_get_metadata_audio_nb (player, mrl);
  for (i = 1; i <= prop; i++)
  {
    int ret;
    uint32_t id = 0;
    char *name = NULL, *lang = NULL;

    ret = mrl_get_metadata_audio (player, mrl, i, &id, &name, &lang);
    if (!ret)
      continue;

    printf (" Meta Audio Stream %u", id);
    if (name)
    {
      printf (" Name: %s", name);
      free (name);
    }
    if (lang)
    {
      printf (" (%s)", lang);
      free (lang);
    }
    printf ("\n");
  }

  /* CDDA/CDDB */

  prop = mrl_get_metadata_cd (player, mrl, MRL_METADATA_CD_DISCID);
  if (prop)
    printf (" Meta CD DiscID: %08x\n", prop);

  prop = mrl_get_metadata_cd (player, mrl, MRL_METADATA_CD_TRACKS);
  if (prop)
  {
    unsigned int i;

    printf (" Meta CD Tracks: %i\n", prop);

    for (i = 1; i <= prop; i++)
    {
      uint32_t length = 0;
      meta = mrl_get_metadata_cd_track (player, mrl, i, &length);

      if (meta)
      {
        printf (" Meta CD Track %i Name: %s (%i sec)\n",
                i, meta, length / 1000);
        free (meta);
      }
      else
        printf (" Meta CD Track %i Length: %i sec\n", i, length / 1000);
    }
  }

  /* DVD/DVDNAV */

  meta = mrl_get_metadata_dvd (player, mrl, (uint8_t *) &prop);
  if (meta)
  {
    printf (" Meta DVD VolumeID: %s\n", meta);
    free (meta);
  }

  if (prop)
  {
    unsigned int i;

    printf (" Meta DVD Titles: %i\n", prop);

    for (i = 1; i <= prop; i++)
    {
      uint32_t chapters, angles, length;

      chapters = mrl_get_metadata_dvd_title (player, mrl, i,
                                             MRL_METADATA_DVD_TITLE_CHAPTERS);
      angles = mrl_get_metadata_dvd_title (player, mrl, i,
                                           MRL_METADATA_DVD_TITLE_ANGLES);
      length = mrl_get_metadata_dvd_title (player, mrl, i,
                                           MRL_METADATA_DVD_TITLE_LENGTH);

      printf (" Meta DVD Title %i (%.2f sec), Chapters: %i, Angles: %i\n",
              i, length / 1000.0, chapters, angles);
    }
  }
}

int
main (int argc, char **argv)
{
  player_t *player;
  player_init_param_t param;
  player_type_t type = PLAYER_TYPE_DUMMY;
  player_vo_t vo = PLAYER_VO_AUTO;
  player_ao_t ao = PLAYER_AO_AUTO;
  uint32_t input;
  int run = 1;
  int volume = 85;
  int time_pos, percent_pos;
  float speed = 1.0;
  int loop = 0, shuffle = 0;
  int visibility = 0, osd = 0;
  float ar = 0.0;
  player_loop_t loop_mode = PLAYER_LOOP_DISABLE;
  player_verbosity_level_t verbosity = PLAYER_MSG_WARNING;
  player_pb_t pb_mode = PLAYER_PB_SINGLE;
  player_quality_level_t quality = PLAYER_QUALITY_NORMAL;

  int c, index;
  const char *const short_options = "hvp:a:g:q:";
  const struct option long_options [] = {
    {"help",    no_argument,       0, 'h' },
    {"verbose", no_argument,       0, 'v' },
    {"player",  required_argument, 0, 'p' },
    {"audio",   required_argument, 0, 'a' },
    {"video",   required_argument, 0, 'g' },
    {"quality", required_argument, 0, 'q' },
    {0,         0,                 0,  0  }
  };

  /* command line argument processing */
  while (1)
  {
    c = getopt_long (argc, argv, short_options, long_options, &index);

    if (c == EOF)
      break;

    switch (c)
    {
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
      else if (verbosity == PLAYER_MSG_WARNING)
        verbosity = PLAYER_MSG_INFO;
      else
        verbosity = PLAYER_MSG_VERBOSE;
      break;

    case 'p':
      if (!strcmp (optarg, "mplayer"))
#ifdef HAVE_MPLAYER
        type = PLAYER_TYPE_MPLAYER;
#else /* HAVE_MPLAYER */
        printf ("MPlayer not supported, dummy player used instead!\n");
#endif /* !HAVE_MPLAYER */
      if (!strcmp (optarg, "xine"))
#ifdef HAVE_XINE
        type = PLAYER_TYPE_XINE;
#else /* HAVE_XINE */
        printf ("xine not supported, dummy player used instead!\n");
#endif /* !HAVE_XINE */
      if (!strcmp (optarg, "vlc"))
#ifdef HAVE_VLC
        type = PLAYER_TYPE_VLC;
#else /* HAVE_VLC */
        printf ("VLC not supported, dummy player used instead!\n");
#endif /* !HAVE_VLC */
      if (!strcmp (optarg, "gstreamer") || !strcmp (optarg, "gst"))
#ifdef HAVE_GSTREAMER
        type = PLAYER_TYPE_GSTREAMER;
#else /* HAVE_GSTREAMER */
        printf ("GStreamer not supported, dummy player used instead!\n");
#endif /* !HAVE_GSTREAMER */
      break;

    case 'a':
      if (!strcmp (optarg, "alsa"))
        ao = PLAYER_AO_ALSA;
      else if (!strcmp (optarg, "oss"))
        ao = PLAYER_AO_OSS;
      else if (!strcmp (optarg, "pulse"))
        ao = PLAYER_AO_PULSE;
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
      else if (!strcmp (optarg, "gl"))
        vo = PLAYER_VO_GL;
      else if (!strcmp (optarg, "vdpau"))
        vo = PLAYER_VO_VDPAU;
      else if (!strcmp (optarg, "fb"))
        vo = PLAYER_VO_FB;
      else if (!strcmp (optarg, "directfb"))
        vo = PLAYER_VO_DIRECTFB;
      else if (!strcmp (optarg, "vaapi"))
        vo = PLAYER_VO_VAAPI;
      else if (!strcmp (optarg, "v4l2"))
        vo = PLAYER_VO_V4L2;
      else if (!strcmp (optarg, "null"))
        vo = PLAYER_VO_NULL;
      break;

    case 'q':
      if (!strcmp (optarg, "0"))
        quality = PLAYER_QUALITY_NORMAL;
      else if (!strcmp (optarg, "1"))
        quality = PLAYER_QUALITY_LOW;
      else if (!strcmp (optarg, "2"))
        quality = PLAYER_QUALITY_LOWEST;
      break;

    default:
      printf (TESTPLAYER_HELP);
      return -1;
    }
  }

#if defined (HAVE_WIN_XCB) && defined (USE_XLIB_HACK)
  XInitThreads ();
#endif /* HAVE_WIN_XCB && USE_XLIB_HACK */

  memset (&param, 0, sizeof (player_init_param_t));
  param.ao       = ao;
  param.vo       = vo;
  param.event_cb = event_cb;
  param.quality  = quality;

  player = player_init (type, verbosity, &param);

  if (!player)
    return -1;

  /* these arguments are files */
  if (optind < argc)
  {
    do
    {
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

  printf (TESTPLAYER_COMMANDS);

  /* main loop */
  while (run)
  {
    printf ("action> ");
    fflush (stdout);
    input = getch ();

    switch (input)
    {
    case '#':
      pb_mode = pb_mode == PLAYER_PB_SINGLE ? PLAYER_PB_AUTO : PLAYER_PB_SINGLE;
      player_set_playback (player, pb_mode);
      printf ("PLAYBACK %s\n", pb_mode == PLAYER_PB_AUTO ? "AUTO" : "SINGLE");
      break;
    case '.':
    {
      static int mode;

      loop = !loop ? 2 : (loop > 0 ? -1 : 0);
      if (!loop)
      {
        loop_mode = PLAYER_LOOP_DISABLE;
        mode = mode ? 0 : 1;
      }

      if (mode && loop)
        loop_mode = PLAYER_LOOP_ELEMENT;
      else if (loop)
        loop_mode = PLAYER_LOOP_PLAYLIST;

      player_set_loop (player, loop_mode, loop);
      printf ("LOOP %s %i (playback auto must be enabled: key '#')\n",
              loop_mode == PLAYER_LOOP_ELEMENT ? "ELEMENT" :
              (loop_mode == PLAYER_LOOP_PLAYLIST ? "PLAYLIST" : "DISABLE"),
              loop);
      break;
    }
    case ',':
      shuffle = shuffle ? 0 : 1;
      player_set_shuffle (player, shuffle);
      printf ("SHUFFLE %s (playback auto must be enabled: key '#')\n",
              shuffle ? "ON" : "OFF");
      break;
    case '%':
    {
      const char *text = "The quick brown fox jumps over the lazy dog.";
      player_osd_show_text (player, text, 0, 0, 5000);
      printf ("OSD SHOW TEXT (5s): %s\n", text);
      break;
    }
    case 'k':
      osd = !osd;
      player_osd_state (player, osd);
      break;
    case '+':
      speed += 0.1;
      if (speed > 100.0)
        speed = 100.0;
      player_playback_speed (player, speed);
      printf ("SPEED %.2f\n", speed);
      break;
    case '-':
      speed -= 0.1;
      if (speed < 0.1)
        speed = 0.1;
      player_playback_speed (player, speed);
      printf ("SPEED %.2f\n", speed);
      break;
    case '[':
      player_audio_set_delay (player, -100, 0);
      printf ("AUDIO DELAY -100 ms\n");
      break;
    case ']':
      player_audio_set_delay (player, 100, 0);
      printf ("AUDIO DELAY +100 ms\n");
      break;
    case '{':
      player_radio_channel_prev (player);
      printf ("RADIO CHANNEL PREV\n");
      break;
    case '}':
      player_radio_channel_next (player);
      printf ("RADIO CHANNEL NEXT\n");
      break;
    case '(':
      player_playback_seek_chapter (player, -1, 0);
      printf ("SEEK CHAPTER -1\n");
      break;
    case ')':
      player_playback_seek_chapter (player, 1, 0);
      printf ("SEEK CHAPTER +1\n");
      break;
    case '0':   /* increase volume */
      if (++volume > 100)
        volume = 100;
      player_audio_volume_set (player, volume);
      printf ("VOLUME %i\n", volume);
      break;
    case '1':   /* 5s backward */
      player_playback_seek (player, -5000, PLAYER_PB_SEEK_RELATIVE);
      printf ("SEEK -5 sec.\n");
      break;
    case '2':   /* 5s forward */
      player_playback_seek (player, 5000, PLAYER_PB_SEEK_RELATIVE);
      printf ("SEEK +5 sec.\n");
      break;
    case '3':
      player_audio_prev (player);
      printf ("AUDIO PREV\n");
      break;
    case '4':
      player_audio_next (player);
      printf ("AUDIO NEXT\n");
      break;
    case '5':
      player_subtitle_prev (player);
      printf ("SUBTITLE PREV\n");
      break;
    case '6':
      player_subtitle_next (player);
      printf ("SUBTITLE NEXT\n");
      break;
    case '7':
      player_tv_channel_prev (player);
      printf ("TV CHANNEL PREV\n");
      break;
    case '8':
      player_tv_channel_next (player);
      printf ("TV CHANNEL NEXT\n");
      break;
    case '9':   /* decrease volume */
      if (--volume < 0)
        volume = 0;
      player_audio_volume_set (player, volume);
      printf ("VOLUME %i\n", volume);
      break;
    case 'a':
      ar = ar ? 0.0 : 16.0 / 9.0;
      player_video_set_aspect_ratio (player, ar);
      printf ("ASPECT RATIO %.2f\n", ar);
      break;
    case 'b':   /* start the previous stream in the playlist */
      player_mrl_previous (player);
      printf ("PREVIOUS STREAM\n");
      break;
    case 'i':   /* print current time position */
      time_pos = player_get_time_pos (player);
      percent_pos = player_get_percent_pos (player);
      printf ("POSITION: %.2f sec (%i%%)\n",
              time_pos < 0 ? 0.0 : (float) time_pos / 1000.0,
              percent_pos < 0 ? 0 : percent_pos);
      break;
    case 'j':   /* take a video snapshot */
    {
      int n, p = 0;
      printf ("position [second]: ");
      n = scanf ("%u", &p);
      if (n != 1)
        p = 0;
      mrl_video_snapshot (player, NULL, p, MRL_SNAPSHOT_JPG, "./snapshot.jpg");
      printf ("SNAPSHOT: (pos %i sec) saved to ./snapshot.jpg\n", p);
      break;
    }
    case 'l':   /* load a stream in the playlist */
      load_media (player);
      break;
    case 'm':   /* set/unset mute */
      if (player_audio_mute_get (player) != PLAYER_MUTE_ON)
      {
        player_audio_mute_set (player, PLAYER_MUTE_ON);
        printf ("MUTE\n");
      }
      else
      {
        player_audio_mute_set (player, PLAYER_MUTE_OFF);
        printf ("UNMUTE\n");
      }
      break;
    case 'n':   /* start the next stream in the playlist */
      player_mrl_next (player);
      printf ("NEXT STREAM\n");
      break;
    case 'c':   /* continue with the next stream accordingly to the pb mode. */
      player_mrl_continue (player);
      printf ("CONTINUE\n");
      break;
    case 'o':   /* pause the current playback */
      player_playback_pause (player);
      printf ("PAUSE\n");
      break;
    case 'p':   /* start a new playback */
      player_playback_start (player);
      printf ("START PLAYBACK\n");
      volume = player_audio_volume_get (player);
      speed = 1.0;
      break;
    case 'q':   /* quit libplayer-test */
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
    case 'u':
      visibility = visibility ? 0 : 1;
      player_subtitle_set_visibility (player, visibility);
      printf ("SUBTITLE VISIBILITY %i\n", visibility);
      break;
    case 'v':   /* print properties and metadata */
      show_info (player, player_mrl_get_current (player));
      break;
    case 'y':
    case 'z':
    {
      char channel[16] = "";
      printf ("channel: ");
      while (!*channel)
      {
        char *r;
        r = fgets (channel, sizeof (channel), stdin);
        if (!r)
          continue;
        *(channel + strlen (channel) - 1) = '\0';
      }
      if (input == 'y')
        player_radio_channel_select (player, channel);
      else
        player_tv_channel_select (player, channel);
      break;
    }
    case 0x1B5B41: /* UP */
      player_dvd_nav (player, PLAYER_DVDNAV_UP);
      printf ("DVDNAV UP\n");
      break;
    case 0x1B5B42: /* DOWN */
      player_dvd_nav (player, PLAYER_DVDNAV_DOWN);
      printf ("DVDNAV DOWN\n");
      break;
    case 0x1B5B44: /* LEFT */
      player_dvd_nav (player, PLAYER_DVDNAV_LEFT);
      printf ("DVDNAV LEFT\n");
      break;
    case 0x1B5B43: /* RIGHT */
      player_dvd_nav (player, PLAYER_DVDNAV_RIGHT);
      printf ("DVDNAV RIGHT\n");
      break;
    case 0xA: /* ENTER */
      player_dvd_nav (player, PLAYER_DVDNAV_SELECT);
      printf ("DVDNAV SELECT\n");
      break;
    case 0x7F: /* BACKSPACE */
      player_dvd_nav (player, PLAYER_DVDNAV_MENU);
      printf ("DVDNAV MENU\n");
      break;
    default:
      fprintf (stderr, "ERROR: Command unknown!\n");
      printf (TESTPLAYER_COMMANDS);
    }
  }

  player_uninit (player);

  return 0;
}
