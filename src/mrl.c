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

static void
mrl_resource_local_free (mrl_resource_local_args_t *args)
{
  if (!args)
    return;
  
  if (args->location)
    free (args->location);
}

static void
mrl_resource_cd_free (mrl_resource_cd_args_t *args)
{
  if (!args)
    return;
  
  if (args->device)
    free (args->device);
}

static void
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

static void
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

static void
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

void
mrl_free (mrl_t *mrl, int recursive)
{
  if (!mrl)
    return;

  if (mrl->subs)
  {
    char **sub = mrl->subs;
    while (*sub)
    {
      free (*sub);
      (*sub)++;
    }
    free (mrl->subs);
  }

  if (mrl->prop)
    mrl_properties_free (mrl->prop);
  if (mrl->meta)
    mrl_metadata_free (mrl->meta);

  if (mrl->priv)
  {
    switch (mrl->resource)
    {
    case MRL_RESOURCE_FIFO:
    case MRL_RESOURCE_FILE:
    case MRL_RESOURCE_STDIN:
      mrl_resource_local_free (mrl->priv);
      break;
      
    case MRL_RESOURCE_CDDA:
    case MRL_RESOURCE_CDDB:
      mrl_resource_cd_free (mrl->priv);
      break;
      
    case MRL_RESOURCE_DVD:
    case MRL_RESOURCE_DVDNAV:
    case MRL_RESOURCE_VCD:
      mrl_resource_videodisc_free (mrl->priv);
      break;
      
    case MRL_RESOURCE_DVB:
    case MRL_RESOURCE_PVR:
    case MRL_RESOURCE_RADIO:
    case MRL_RESOURCE_TV:
      mrl_resource_tv_free (mrl->priv);
      break;
      
    case MRL_RESOURCE_FTP: 
    case MRL_RESOURCE_HTTP:
    case MRL_RESOURCE_MMS:
    case MRL_RESOURCE_RTP:
    case MRL_RESOURCE_RTSP:
    case MRL_RESOURCE_SMB:
    case MRL_RESOURCE_TCP:
    case MRL_RESOURCE_UDP:
    case MRL_RESOURCE_UNSV:
      mrl_resource_network_free (mrl->priv);
      break;
      
    default:
      break;
    }
    free (mrl->priv);
  }
  
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

static void
mrl_retrieve_properties (player_t *player, mrl_t *mrl)
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
mrl_get_property (player_t *player, mrl_t *mrl, mrl_properties_type_t p)
{
  mrl_properties_t *prop;

  if (!player)
    return 0;

  /* try to use internal mrl? */
  if (!mrl && player->mrl)
    mrl = player->mrl;
  else if (!mrl)
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
mrl_get_audio_codec (player_t *player, mrl_t *mrl)
{
  mrl_properties_t *prop;
  mrl_properties_audio_t *audio;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return NULL;

  /* try to use internal mrl? */
  if (!mrl && player->mrl)
    mrl = player->mrl;
  else if (!mrl)
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
mrl_get_video_codec (player_t *player, mrl_t *mrl)
{
  mrl_properties_t *prop;
  mrl_properties_video_t *video;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return NULL;

  /* try to use internal mrl? */
  if (!mrl && player->mrl)
    mrl = player->mrl;
  else if (!mrl)
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
mrl_get_size (player_t *player, mrl_t *mrl)
{
  mrl_properties_t *prop;

  plog (player, PLAYER_MSG_INFO, MODULE_NAME, __FUNCTION__);

  if (!player)
    return 0;

  /* try to use internal mrl? */
  if (!mrl && player->mrl)
    mrl = player->mrl;
  else if (!mrl)
    return 0;

  if (!mrl->prop)
    mrl_retrieve_properties (player, mrl);

  prop = mrl->prop;
  if (!prop)
    return 0;

  return prop->size;
}

static void
mrl_retrieve_metadata (player_t *player, mrl_t *mrl)
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
mrl_get_metadata (player_t *player, mrl_t *mrl, mrl_metadata_type_t m)
{
  mrl_metadata_t *meta;

  if (!player)
    return NULL;

  /* try to use internal mrl? */
  if (!mrl && player->mrl)
    mrl = player->mrl;
  else if (!mrl)
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

static int
get_list_length (void *list)
{
  void **l = list;
  int n = 0;
  while (*l++)
    n++;
  return n;
}

void
mrl_add_subtitle (mrl_t *mrl, char *subtitle)
{
  char **subs;
  int n;
  
  if (!mrl || !subtitle)
    return;
  
  subs = mrl->subs;
  n = get_list_length (subs) + 1;
  subs = realloc (subs, (n + 1) * sizeof (*subs));
  subs[n] = NULL;
  subs[n - 1] = strdup (subtitle);
}

mrl_t *
mrl_new (player_t *player, mrl_resource_t res, void *args)
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
          "Unsupported resource type (%d)\n", res);
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

mrl_type_t
mrl_get_type (mrl_t *mrl)
{
  if (!mrl)
    return MRL_TYPE_UNKNOWN;

  return mrl->type;
}

mrl_resource_t
mrl_get_resource (mrl_t *mrl)
{
  if (!mrl)
    return MRL_RESOURCE_UNKNOWN;

  return mrl->resource;
}
