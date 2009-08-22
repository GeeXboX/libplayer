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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <locale.h>
#include <string.h>

#include "parse_utils.h"


char *
trim_whitespaces (char *str)
{
  char *its, *ite;

  its = str;
  ite = strchr (str, '\0');

  /* remove whitespaces at the right */
  while (ite > its && (*(ite - 1) == ' '  || *(ite - 1) == '\t' ||
                       *(ite - 1) == '\r' || *(ite - 1) == '\n'))
    ite--;
  *ite = '\0';

  /* remove whitespaces at the left */
  while (*its == ' ' || *its == '\t')
    its++;

  return its;
}

int
count_nb_dec (int dec)
{
  int size = 1;

  while (dec /= 10)
    size++;

  return size;
}

char *
my_strrstr (const char *buf, const char *str)
{
  char *ptr, *res = NULL;

  while ((ptr = strstr (buf, str)))
  {
    res = ptr;
    buf = ptr + strlen (str);
  }

  return res;
}

double
my_atof (const char *nptr)
{
  double res;
  locale_t new_locale;

  new_locale = newlocale (LC_NUMERIC_MASK, "C", NULL);
  res = strtod_l (nptr, NULL, new_locale);
  freelocale (new_locale);

  return res;
}
