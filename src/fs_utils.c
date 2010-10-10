/*
 * GeeXboX libplayer: a multimedia A/V abstraction layer API.
 * Copyright (C) 2008 Mathieu Schroeter <mathieu@schroetersa.ch>
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
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "fs_utils.h"

#define BUFFER_SIZE 512


int
pl_copy_file (const char *src, const char *dst)
{
  char buf[BUFFER_SIZE];
  int infile, outfile, size;
  struct stat st;

  infile = open (src, O_RDONLY, 0);
  if (infile < 0)
    return -1;

  if (fstat (infile, &st) < 0)
  {
    close (infile);
    return -1;
  }

  outfile = open (dst, O_CREAT | O_TRUNC | O_WRONLY, (int) (st.st_mode & 0777));
  if (outfile < 0)
  {
    close (infile);
    return -1;
  }

  while ((size = read (infile, buf, BUFFER_SIZE)) > 0)
  {
    int res = write (outfile, buf, size);
    if (res < size)
    {
      close (outfile);
      close (infile);
      unlink (dst);
      return -1;
    }
  }

  close (outfile);
  close (infile);
  return 0;
}

int
pl_file_exists (const char *file)
{
  struct stat st;

  if (!stat (file, &st))
    return 1;
  return 0;
}

off_t
pl_file_size (const char *file)
{
  struct stat st;

  if (!stat (file, &st))
    return st.st_size;
  return 0;
}
