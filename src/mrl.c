/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006-2008 Benjamin Zores <ben@geexbox.org>
 *                         Mathieu Schroeter <mathieu.schroeter@gamesover.ch>
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
#include <string.h>
#include <inttypes.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"

#define MODULE_NAME "player"

static player_mrl_type_t
mrl_guess_type (mrl_t *mrl)
{
  if (!mrl || !mrl->prop)
    return PLAYER_MRL_TYPE_UNKNOWN;

  if (mrl->prop->video)
    return PLAYER_MRL_TYPE_VIDEO;

  if (mrl->prop->audio)
    return PLAYER_MRL_TYPE_AUDIO;

  return PLAYER_MRL_TYPE_UNKNOWN;
}

static player_mrl_resource_t
mrl_guess_resource (mrl_t *mrl)
{
  int i = 0;
  static const struct {
    const player_mrl_resource_t resource;
    const char *string;
  } resources_list[] = {
    {PLAYER_MRL_RESOURCE_CDDA,    "cdda:"},
    {PLAYER_MRL_RESOURCE_CDDB,    "cddb:"},
    {PLAYER_MRL_RESOURCE_DVB,     "dvb:"},
    {PLAYER_MRL_RESOURCE_DVD,     "dvd:"},
    {PLAYER_MRL_RESOURCE_DVDNAV,  "dvdnav:"},
    {PLAYER_MRL_RESOURCE_FIFO,    "fifo:"},
    {PLAYER_MRL_RESOURCE_FILE,    "file:"},
    {PLAYER_MRL_RESOURCE_FTP,     "ftp:"},
    {PLAYER_MRL_RESOURCE_HTTP,    "http:"},
    {PLAYER_MRL_RESOURCE_MMS,     "mms:"},
    {PLAYER_MRL_RESOURCE_RADIO,   "radio:"},
    {PLAYER_MRL_RESOURCE_RTP,     "rtp:"},
    {PLAYER_MRL_RESOURCE_RTSP,    "rtsp:"},
    {PLAYER_MRL_RESOURCE_SMB,     "smb:"},
    {PLAYER_MRL_RESOURCE_STDIN,   "stdin:"},
    {PLAYER_MRL_RESOURCE_TCP,     "tcp:"},
    {PLAYER_MRL_RESOURCE_TV,      "tv:"},
    {PLAYER_MRL_RESOURCE_UDP,     "udp:"},
    {PLAYER_MRL_RESOURCE_VCD,     "vcd:"},
    {PLAYER_MRL_RESOURCE_UNKNOWN, NULL}
  };

  if (!mrl || !mrl->name)
    return PLAYER_MRL_RESOURCE_UNKNOWN;

  /* when no resource is used in the name */
  if (!strstr (mrl->name, ":"))
    return PLAYER_MRL_RESOURCE_FILE;

  while (resources_list[i].string) {
    if (strstr (mrl->name, resources_list[i].string))
      return resources_list[i].resource;
    i++;
  }

  return PLAYER_MRL_RESOURCE_UNKNOWN;
}

mrl_properties_audio_t *
mrl_properties_audio_new (void)
{
  mrl_properties_audio_t *audio;

  audio = calloc (1, sizeof (mrl_properties_audio_t));

  return audio;
}

void
mrl_properties_audio_free (mrl_properties_audio_t *audio)
{
  if (!audio)
    return;

  if (audio->codec)
    free (audio->codec);
  free (audio);
}

mrl_properties_video_t *
mrl_properties_video_new (void)
{
  mrl_properties_video_t *video;

  video = calloc (1, sizeof (mrl_properties_video_t));

  return video;
}

void
mrl_properties_video_free (mrl_properties_video_t *video)
{
  if (!video)
    return;

  if (video->codec)
    free (video->codec);
  free (video);
}

mrl_properties_t *
mrl_properties_new (void)
{
  mrl_properties_t *prop;

  prop = calloc (1, sizeof (mrl_properties_t));

  return prop;
}

void
mrl_properties_free (mrl_properties_t *prop)
{
  if (!prop)
    return;

  if (prop->audio)
    mrl_properties_audio_free (prop->audio);
  if (prop->video)
    mrl_properties_video_free (prop->video);
  free (prop);
}

mrl_metadata_t *
mrl_metadata_new (void)
{
  mrl_metadata_t *meta;

  meta = calloc (1, sizeof (mrl_metadata_t));

  return meta;
}

void
mrl_metadata_free (mrl_metadata_t *meta)
{
  if (!meta)
    return;

  if (meta->title)
    free (meta->title);
  if (meta->artist)
    free (meta->artist);
  if (meta->genre)
    free (meta->genre);
  if (meta->album)
    free (meta->album);
  if (meta->year)
    free (meta->year);
  if (meta->track)
    free (meta->track);
  if (meta->comment)
    free (meta->comment);
  free (meta);
}

void
mrl_free (mrl_t *mrl, int recursive)
{
  if (!mrl)
    return;

  if (mrl->name)
    free (mrl->name);
  if (mrl->subtitle)
    free (mrl->subtitle);
  if (mrl->cover)
    free (mrl->cover);

  if (mrl->prop)
    mrl_properties_free (mrl->prop);
  if (mrl->meta)
    mrl_metadata_free (mrl->meta);

  if (recursive && mrl->next)
    mrl_free (mrl->next, 1);

  free (mrl);
}

void
mrl_list_free (mrl_t *mrl)
{
  if (!mrl)
    return;

  /* go to the very begining of the playlist in case of recursive free() */
  while (mrl->prev)
    mrl = mrl->prev;

  mrl_free (mrl, 1);
}

int
mrl_uses_vo (mrl_t *mrl)
{
  if (!mrl || !mrl->prop)
    return -1;

  if (mrl->prop->video)
    return 0;

  return 1;
}

int
mrl_uses_ao (mrl_t *mrl)
{
  if (!mrl || !mrl->prop)
    return -1;

  if (mrl->prop->audio)
    return 0;

  return 1;
}

mrl_t *
player_get_mrl (player_t *player)
{
  if (!player)
    return NULL;

  return player->mrl;
}

void
player_mrl_append (player_t *player,
                   char *location, char *subtitle, player_add_mrl_t when)
{
  mrl_t *mrl = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player || !location)
    return;

  mrl = mrl_new (player, location, subtitle);
  if (!mrl)
    return;

  /* create/expand the playlist */
  if (!player->mrl) /* empty list */
    player->mrl = mrl;
  else /* create double-linked playlist, appending new MRL to the bottom */
  {
    mrl_t *list;

    list = player->mrl;
    while (list->next)
      list = list->next;
    list->next = mrl;
    mrl->prev = list;
  }

  /* play it now ? */
  if (when == PLAYER_ADD_MRL_NOW)
  {
    player_playback_stop (player);
    player->mrl = mrl;
    player_playback_start (player);
  }
}

void
player_mrl_set (player_t *player, char *location, char *subtitle)
{
  mrl_t *mrl = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player || !location)
    return;

  mrl = mrl_new (player, location, subtitle);
  if (!mrl)
    return;

  if (player->mrl) {
    player_playback_stop (player);
    mrl->prev = player->mrl->prev;
    mrl->next = player->mrl->next;
    mrl_free (player->mrl, 0);
  }

  player->mrl = mrl;
}

void
player_mrl_remove (player_t *player)
{
  mrl_t *mrl, *mrl_p = NULL, *mrl_n = NULL;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  mrl = player->mrl;
  if (!mrl)
    return;

  mrl_p = mrl->prev;
  mrl_n = mrl->next;

  player_playback_stop (player);
  mrl_free (mrl, 0);

  /* link previous with the next and use the next as the current MRL */
  if (mrl_p && mrl_n) {
    mrl_p->next = mrl_n;
    mrl_n->prev = mrl_p;
    player->mrl = mrl_n;
  }
  /* use the previous as the current MRL */
  else if (mrl_p) {
    mrl_p->next = NULL;
    player->mrl = mrl_p;
  }
  /* use the next as the current MRL */
  else if (mrl_n) {
    mrl_n->prev = NULL;
    player->mrl = mrl_n;
  }
  else
    player->mrl = NULL;
}

void
player_mrl_remove_all (player_t *player)
{
  mrl_t *mrl;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  mrl = player->mrl;
  if (!mrl)
    return;

  player_playback_stop (player);

  mrl_list_free (mrl);
  player->mrl = NULL;
}

void
player_mrl_previous (player_t *player)
{
  mrl_t *mrl;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  mrl = player->mrl;
  if (!mrl || !mrl->prev)
    return;

  player_playback_stop (player);
  player->mrl = mrl->prev;
  player_playback_start (player);
}

void
player_mrl_next (player_t *player)
{
  mrl_t *mrl;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return;

  mrl = player->mrl;
  if (!mrl || !mrl->next)
    return;

  player_playback_stop (player);
  player->mrl = mrl->next;
  player_playback_start (player);
}

static void
player_mrl_retrieve_properties (player_t *player, mrl_t *mrl)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player || !mrl)
    return;

  if (mrl->prop) /* already retrieved */
    return;

  mrl->prop = mrl_properties_new ();

  /* player specific init */
  if (player->funcs->mrl_retrieve_props)
    player->funcs->mrl_retrieve_props (player, mrl);
}

uint32_t
player_mrl_get_properties (player_t *player, mrl_t *mrl, player_properties_t p)
{
  mrl_properties_t *prop;

  if (!player || !mrl)
    return 0;

  if (!mrl->prop)
    player_mrl_retrieve_properties (player, mrl);

  prop = mrl->prop;
  if (!prop)
    return 0;

  switch (p) {
  case PLAYER_PROPERTY_SEEKABLE:
    return prop->seekable;

  case PLAYER_PROPERTY_LENGTH:
    return prop->length;

  case PLAYER_PROPERTY_AUDIO_BITRATE:
    return prop->audio ? prop->audio->bitrate : 0;

  case PLAYER_PROPERTY_AUDIO_BITS:
    return prop->audio ? prop->audio->bits : 0;

  case PLAYER_PROPERTY_AUDIO_CHANNELS:
    return prop->audio ? prop->audio->channels : 0;

  case PLAYER_PROPERTY_AUDIO_SAMPLERATE:
    return prop->audio ? prop->audio->samplerate : 0;

  case PLAYER_PROPERTY_VIDEO_BITRATE:
    return prop->video ? prop->video->bitrate : 0;

  case PLAYER_PROPERTY_VIDEO_WIDTH:
    return prop->video ? prop->video->width : 0;

  case PLAYER_PROPERTY_VIDEO_HEIGHT:
    return prop->video ? prop->video->height : 0;

  case PLAYER_PROPERTY_VIDEO_ASPECT:
    return prop->video ? prop->video->aspect : 0;

  case PLAYER_PROPERTY_VIDEO_CHANNELS:
    return prop->video ? prop->video->channels : 0;

  case PLAYER_PROPERTY_VIDEO_STREAMS:
    return prop->video ? prop->video->streams : 0;

  case PLAYER_PROPERTY_VIDEO_FRAMEDURATION:
    return prop->video ? prop->video->frameduration : 0;

  default:
    return 0;
  }
}

char *
player_mrl_get_audio_codec (player_t *player, mrl_t *mrl)
{
  mrl_properties_t *prop;
  mrl_properties_audio_t *audio;

  if (!player || !mrl)
    return NULL;

  if (!mrl->prop)
    player_mrl_retrieve_properties (player, mrl);

  prop = mrl->prop;
  if (!prop)
    return NULL;

  audio = prop->audio;
  return audio ? strdup (audio->codec) : NULL;
}

char *
player_mrl_get_video_codec (player_t *player, mrl_t *mrl)
{
  mrl_properties_t *prop;
  mrl_properties_video_t *video;

  if (!player || !mrl)
    return NULL;

  if (!mrl->prop)
    player_mrl_retrieve_properties (player, mrl);

  prop = mrl->prop;
  if (!prop)
    return NULL;

  video = prop->video;
  return video ? strdup (video->codec) : NULL;
}

off_t
player_mrl_get_size (player_t *player, mrl_t *mrl)
{
  mrl_properties_t *prop;

  if (!player || !mrl)
    return 0;

  if (!mrl->prop)
    player_mrl_retrieve_properties (player, mrl);

  prop = mrl->prop;
  if (!prop)
    return 0;

  return prop->size;
}

static void
player_mrl_retrieve_metadata (player_t *player, mrl_t *mrl)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player || !mrl)
    return;

  if (mrl->meta) /* already retrieved */
    return;

  mrl->meta = mrl_metadata_new ();

  /* player specific init */
  if (player->funcs->mrl_retrieve_meta)
    player->funcs->mrl_retrieve_meta (player, mrl);
}

char *
player_mrl_get_metadata (player_t *player, mrl_t *mrl, player_metadata_t m)
{
  mrl_metadata_t *meta;

  if (!player || !mrl)
    return NULL;

  if (!mrl->meta)
    player_mrl_retrieve_metadata (player, mrl);

  meta = mrl->meta;
  if (!meta)
    return NULL;

  switch (m) {
  case PLAYER_METADATA_TITLE:
    return meta->title ? strdup (meta->title) : NULL;

  case PLAYER_METADATA_ARTIST:
    return meta->artist ? strdup (meta->artist) : NULL;

  case PLAYER_METADATA_GENRE:
    return meta->genre ? strdup (meta->genre) : NULL;

  case PLAYER_METADATA_ALBUM:
    return meta->album ? strdup (meta->album) : NULL;

  case PLAYER_METADATA_YEAR:
    return meta->year ? strdup (meta->year) : NULL;

  case PLAYER_METADATA_TRACK:
    return meta->track ? strdup (meta->track) : NULL;

  case PLAYER_METADATA_COMMENT:
    return meta->comment ? strdup (meta->comment) : NULL;

  default:
    return NULL;
  }
}

mrl_t *
mrl_new (player_t *player, char *name, char *subtitle)
{
  mrl_t *mrl = NULL;

  if (!player || !name)
    return NULL;

  mrl = calloc (1, sizeof (mrl_t));
  mrl->name = strdup (name);
  mrl->subtitle = subtitle ? strdup (subtitle) : NULL;

  player_mrl_retrieve_properties (player, mrl);

  mrl->type = mrl_guess_type (mrl);   /* can guess only if properties exist */
  mrl->resource = mrl_guess_resource (mrl);

  return mrl;
}
