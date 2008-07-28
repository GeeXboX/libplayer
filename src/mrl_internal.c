/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2006-2008 Benjamin Zores <ben@geexbox.org>
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
#include <string.h>
#include <inttypes.h>

#include "player.h"
#include "player_internals.h"
#include "logs.h"
#include "playlist.h"

#define MODULE_NAME "mrl"

/*****************************************************************************/
/*                          MRL Internal functions                           */
/*****************************************************************************/

static mrl_type_t
mrl_guess_type (mrl_t *mrl)
{
  if (!mrl || !mrl->prop)
    return MRL_TYPE_UNKNOWN;

  if (mrl->prop->video)
    return MRL_TYPE_VIDEO;

  if (mrl->prop->audio)
    return MRL_TYPE_AUDIO;

  return MRL_TYPE_UNKNOWN;
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

mrl_metadata_cd_track_t *
mrl_metadata_cd_track_new (void)
{
  mrl_metadata_cd_track_t *track;

  track = calloc (1, sizeof (mrl_metadata_cd_track_t));

  return track;
}

void
mrl_metadata_cd_track_append (mrl_metadata_cd_t *cd,
                              mrl_metadata_cd_track_t *track)
{
  mrl_metadata_cd_track_t *track_p;

  if (!cd || !track)
    return;

  if (!cd->track)
  {
    cd->track = track;
    return;
  }

  track_p = cd->track;
  while (track_p->next)
    track_p = track_p->next;

  track_p->next = track;
}

static mrl_metadata_cd_t *
mrl_metadata_cd_new (void)
{
  mrl_metadata_cd_t *cd;

  cd = calloc (1, sizeof (mrl_metadata_cd_t));

  return cd;
}

mrl_metadata_t *
mrl_metadata_new (mrl_resource_t res)
{
  mrl_metadata_t *meta;

  meta = calloc (1, sizeof (mrl_metadata_t));
  if (!meta)
    return NULL;

  switch (res)
  {
  case MRL_RESOURCE_CDDA:
  case MRL_RESOURCE_CDDB:
    meta->priv = mrl_metadata_cd_new ();
    break;

  default:
    break;
  }

  return meta;
}

static void
mrl_metadata_cd_free (mrl_metadata_cd_t *cd)
{
  mrl_metadata_cd_track_t *track, *track_p;

  if (!cd)
    return;

  track = cd->track;
  while (track)
  {
    if (track->name)
      free (track->name);
    track_p = track;
    track = track->next;
    free (track_p);
  }
}

void
mrl_metadata_free (mrl_metadata_t *meta, mrl_resource_t res)
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

  if (meta->priv)
  {
    switch (res)
    {
    case MRL_RESOURCE_CDDA:
    case MRL_RESOURCE_CDDB:
      mrl_metadata_cd_free (meta->priv);
      break;

    default:
      break;
    }
    free (meta->priv);
  }

  free (meta);
}

void
mrl_resource_local_free (mrl_resource_local_args_t *args)
{
  if (!args)
    return;

  if (args->location)
    free (args->location);
}

void
mrl_resource_cd_free (mrl_resource_cd_args_t *args)
{
  if (!args)
    return;

  if (args->device)
    free (args->device);
}

void
mrl_resource_videodisc_free (mrl_resource_videodisc_args_t *args)
{
  if (!args)
    return;

  if (args->device)
    free (args->device);
  if (args->audio_lang)
    free (args->audio_lang);
  if (args->sub_lang)
    free (args->sub_lang);
}

void
mrl_resource_tv_free (mrl_resource_tv_args_t *args)
{
  if (!args)
    return;

  if (args->device)
    free (args->device);
  if (args->driver)
    free (args->driver);
  if (args->output_format)
    free (args->output_format);
  if (args->norm)
    free (args->norm);
}

void
mrl_resource_network_free (mrl_resource_network_args_t *args)
{
  if (!args)
    return;

  if (args->url)
    free (args->url);
  if (args->username)
    free (args->username);
  if (args->password)
    free (args->password);
  if (args->user_agent)
    free (args->user_agent);
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

static inline void
mrl_use_internal (player_t *player, mrl_t **mrl)
{
  if (!mrl || *mrl)
    return;

  *mrl = playlist_get_mrl (player->playlist);
}

static void
mrl_properties_plog (player_t *player, mrl_t *mrl)
{
  mrl_properties_t *prop;
  mrl_properties_audio_t *audio;
  mrl_properties_video_t *video;

  if (!player || !mrl)
    return;

  prop = mrl->prop;
  if (!prop)
    return;

  audio = mrl->prop->audio;
  video = mrl->prop->video;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, "File Size: %.2f MB",
        (float) mrl->prop->size / 1024 / 1024);

  plog (player, PLAYER_MSG_INFO,
        MODULE_NAME, "Seekable: %i", mrl->prop->seekable);

  plog (player, PLAYER_MSG_INFO,
        MODULE_NAME, "Length: %i ms", mrl->prop->length);

  if (audio)
  {
    if (audio->codec)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Audio Codec: %s", audio->codec);

    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Audio Bitrate: %i kbps", audio->bitrate / 1000);

    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Audio Bits: %i bps", audio->bits);

    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Audio Channels: %i", audio->channels);

    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Audio Sample Rate: %i Hz", audio->samplerate);
  }

  if (video)
  {
    if (video->codec)
      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Video Codec: %s", video->codec);

    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Video Bitrate: %i kbps", video->bitrate / 1000);

    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Video Width: %i", video->width);

    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Video Height: %i", video->height);

    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Video Aspect: %i", video->aspect);

    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Video Channels: %i", video->channels);

    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Video Streams: %i", video->streams);

    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Video Framerate: %i", video->frameduration);
  }
}

void
mrl_retrieve_properties (player_t *player, mrl_t *mrl)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player || !mrl)
    return;

  if (mrl->prop) /* already retrieved */
    return;

  mrl->prop = mrl_properties_new ();

  /* player specific retrieve_props() */
  if (player->funcs->mrl_retrieve_props)
    player->funcs->mrl_retrieve_props (player, mrl);

  mrl_properties_plog (player, mrl);
}

static void
mrl_metadata_plog (player_t *player, mrl_t *mrl)
{
  mrl_metadata_t *meta;

  if (!player || !mrl)
    return;

  meta = mrl->meta;
  if (!meta)
    return;

  if (meta->title)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Title: %s", meta->title);

  if (meta->artist)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Artist: %s", meta->artist);

  if (meta->genre)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Genre: %s", meta->genre);

  if (meta->album)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Album: %s", meta->album);

  if (meta->year)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Year: %s", meta->year);

  if (meta->track)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Track: %s", meta->track);

  if (meta->comment)
    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta Comment: %s", meta->comment);

  if (!meta->priv)
    return;

  switch (mrl->resource)
  {
  case MRL_RESOURCE_CDDA:
  case MRL_RESOURCE_CDDB:
  {
    int cnt = 1;
    mrl_metadata_cd_t *cd = meta->priv;
    mrl_metadata_cd_track_t *track = cd->track;

    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta CD DiscID: %08lx", cd->discid);

    plog (player, PLAYER_MSG_INFO,
          MODULE_NAME, "Meta CD Tracks: %i", cd->tracks);

    while (track)
    {
      if (track->name)
        plog (player, PLAYER_MSG_INFO,
              MODULE_NAME, "Meta CD Track %i Name: %s", cnt, track->name);

      plog (player, PLAYER_MSG_INFO,
            MODULE_NAME, "Meta CD Track %i Length: %i ms", cnt, track->length);

      cnt++;
      track = track->next;
    }
  }

  default:
    break;
  }
}

void
mrl_retrieve_metadata (player_t *player, mrl_t *mrl)
{
  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player || !mrl)
    return;

  if (mrl->meta) /* already retrieved */
    return;

  mrl->meta = mrl_metadata_new (mrl->resource);

  /* player specific init */
  if (player->funcs->mrl_retrieve_meta)
    player->funcs->mrl_retrieve_meta (player, mrl);

  mrl_metadata_plog (player, mrl);
}

/*****************************************************************************/
/*                   MRL Internal (Supervisor) functions                     */
/*****************************************************************************/

uint32_t
mrl_sv_get_property (player_t *player, mrl_t *mrl, mrl_properties_type_t p)
{
  mrl_properties_t *prop;

  if (!player)
    return 0;

  /* try to use internal mrl? */
  mrl_use_internal (player, &mrl);
  if (!mrl)
    return 0;

  if (!mrl->prop)
    mrl_retrieve_properties (player, mrl);

  prop = mrl->prop;
  if (!prop)
    return 0;

  switch (p) {
  case MRL_PROPERTY_SEEKABLE:
    return prop->seekable;

  case MRL_PROPERTY_LENGTH:
    return prop->length;

  case MRL_PROPERTY_AUDIO_BITRATE:
    return prop->audio ? prop->audio->bitrate : 0;

  case MRL_PROPERTY_AUDIO_BITS:
    return prop->audio ? prop->audio->bits : 0;

  case MRL_PROPERTY_AUDIO_CHANNELS:
    return prop->audio ? prop->audio->channels : 0;

  case MRL_PROPERTY_AUDIO_SAMPLERATE:
    return prop->audio ? prop->audio->samplerate : 0;

  case MRL_PROPERTY_VIDEO_BITRATE:
    return prop->video ? prop->video->bitrate : 0;

  case MRL_PROPERTY_VIDEO_WIDTH:
    return prop->video ? prop->video->width : 0;

  case MRL_PROPERTY_VIDEO_HEIGHT:
    return prop->video ? prop->video->height : 0;

  case MRL_PROPERTY_VIDEO_ASPECT:
    return prop->video ? prop->video->aspect : 0;

  case MRL_PROPERTY_VIDEO_CHANNELS:
    return prop->video ? prop->video->channels : 0;

  case MRL_PROPERTY_VIDEO_STREAMS:
    return prop->video ? prop->video->streams : 0;

  case MRL_PROPERTY_VIDEO_FRAMEDURATION:
    return prop->video ? prop->video->frameduration : 0;

  default:
    return 0;
  }
}

char *
mrl_sv_get_audio_codec (player_t *player, mrl_t *mrl)
{
  mrl_properties_t *prop;
  mrl_properties_audio_t *audio;

  if (!player)
    return NULL;

  /* try to use internal mrl? */
  mrl_use_internal (player, &mrl);
  if (!mrl)
    return NULL;

  if (!mrl->prop)
    mrl_retrieve_properties (player, mrl);

  prop = mrl->prop;
  if (!prop)
    return NULL;

  audio = prop->audio;
  return audio && audio->codec ? strdup (audio->codec) : NULL;
}

char *
mrl_sv_get_video_codec (player_t *player, mrl_t *mrl)
{
  mrl_properties_t *prop;
  mrl_properties_video_t *video;

  if (!player)
    return NULL;

  /* try to use internal mrl? */
  mrl_use_internal (player, &mrl);
  if (!mrl)
    return NULL;

  if (!mrl->prop)
    mrl_retrieve_properties (player, mrl);

  prop = mrl->prop;
  if (!prop)
    return NULL;

  video = prop->video;
  return video && video->codec ? strdup (video->codec) : NULL;
}

off_t
mrl_sv_get_size (player_t *player, mrl_t *mrl)
{
  mrl_properties_t *prop;

  if (!player)
    return 0;

  /* try to use internal mrl? */
  mrl_use_internal (player, &mrl);
  if (!mrl)
    return 0;

  if (!mrl->prop)
    mrl_retrieve_properties (player, mrl);

  prop = mrl->prop;
  if (!prop)
    return 0;

  return prop->size;
}

char *
mrl_sv_get_metadata (player_t *player, mrl_t *mrl, mrl_metadata_type_t m)
{
  mrl_metadata_t *meta;

  if (!player)
    return NULL;

  /* try to use internal mrl? */
  mrl_use_internal (player, &mrl);
  if (!mrl)
    return NULL;

  if (!mrl->meta)
    mrl_retrieve_metadata (player, mrl);

  meta = mrl->meta;
  if (!meta)
    return NULL;

  switch (m) {
  case MRL_METADATA_TITLE:
    return meta->title ? strdup (meta->title) : NULL;

  case MRL_METADATA_ARTIST:
    return meta->artist ? strdup (meta->artist) : NULL;

  case MRL_METADATA_GENRE:
    return meta->genre ? strdup (meta->genre) : NULL;

  case MRL_METADATA_ALBUM:
    return meta->album ? strdup (meta->album) : NULL;

  case MRL_METADATA_YEAR:
    return meta->year ? strdup (meta->year) : NULL;

  case MRL_METADATA_TRACK:
    return meta->track ? strdup (meta->track) : NULL;

  case MRL_METADATA_COMMENT:
    return meta->comment ? strdup (meta->comment) : NULL;

  default:
    return NULL;
  }
}

char *
mrl_sv_get_metadata_cd_track (player_t *player,
                              mrl_t *mrl, int trackid, uint32_t *length)
{
  int i;
  mrl_metadata_t *meta;
  mrl_metadata_cd_t *cd;
  mrl_metadata_cd_track_t *track;

  if (!player)
    return NULL;

  /* try to use internal mrl? */
  mrl_use_internal (player, &mrl);
  if (!mrl)
    return NULL;

  if (mrl->resource != MRL_RESOURCE_CDDA && mrl->resource != MRL_RESOURCE_CDDB)
    return NULL;

  if (!mrl->meta)
    mrl_retrieve_metadata (player, mrl);

  meta = mrl->meta;
  if (!meta)
    return NULL;

  cd = meta->priv;
  if (!cd)
    return NULL;

  track = cd->track;
  for (i = 1; i < trackid && track; i++)
    track = track->next;

  /* track unavailable */
  if (i != trackid || !track)
    return NULL;

  if (length)
    *length = track->length;

  return track->name ? strdup (track->name) : NULL;
}

uint32_t
mrl_sv_get_metadata_cd (player_t *player, mrl_t *mrl, mrl_metadata_cd_type_t m)
{
  mrl_metadata_t *meta;
  mrl_metadata_cd_t *cd;

  if (!player)
    return 0;

  /* try to use internal mrl? */
  mrl_use_internal (player, &mrl);
  if (!mrl)
    return 0;

  if (mrl->resource != MRL_RESOURCE_CDDA && mrl->resource != MRL_RESOURCE_CDDB)
    return 0;

  if (!mrl->meta)
    mrl_retrieve_metadata (player, mrl);

  meta = mrl->meta;
  if (!meta)
    return 0;

  cd = meta->priv;
  if (!cd)
    return 0;

  switch (m)
  {
  case MRL_METADATA_CD_DISCID:
    return cd->discid;

  case MRL_METADATA_CD_TRACKS:
    return cd->tracks;

  default:
    return 0;
  }
}

mrl_type_t
mrl_sv_get_type (player_t *player, mrl_t *mrl)
{
  if (!player)
    return MRL_TYPE_UNKNOWN;

  /* try to use internal mrl? */
  mrl_use_internal (player, &mrl);
  if (!mrl)
    return MRL_TYPE_UNKNOWN;

  return mrl->type;
}

mrl_resource_t
mrl_sv_get_resource (player_t *player, mrl_t *mrl)
{
  if (!player)
    return MRL_RESOURCE_UNKNOWN;

  /* try to use internal mrl? */
  mrl_use_internal (player, &mrl);
  if (!mrl)
    return MRL_RESOURCE_UNKNOWN;

  return mrl->resource;
}

mrl_t *
mrl_sv_new (player_t *player, mrl_resource_t res, void *args)
{
  mrl_t *mrl = NULL;
  int support = 0;

  if (!player || !args)
    return NULL;

  /* ensure we provide a valid resource type */
  if (res == MRL_RESOURCE_UNKNOWN)
    return NULL;

  /* ensure player support this resource type */
  if (player->funcs->mrl_supported_res)
    support = player->funcs->mrl_supported_res (player, res);

  if (!support)
  {
    plog (player, PLAYER_MSG_WARNING, MODULE_NAME,
          "Unsupported resource type (%d)", res);
    return NULL;
  }

  mrl = calloc (1, sizeof (mrl_t));

  mrl->subs = NULL;

  mrl->resource = res;
  mrl->priv = args;

  mrl_retrieve_properties (player, mrl);

  mrl->type = mrl_guess_type (mrl);   /* can guess only if properties exist */

  return mrl;
}
