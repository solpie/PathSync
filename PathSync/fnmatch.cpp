/* Copyright (C) 1992 Free Software Foundation, Inc.
This file is part of the GNU C Library.

The GNU C Library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The GNU C Library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with the GNU C Library; see the file COPYING.LIB.  If
not, write to the Free Software Foundation, Inc., 675 Mass Ave,
Cambridge, MA 02139, USA.  */

/* Modified slightly by Brian Berliner <berliner@sun.com> and
   Jim Blandy <jimb@cyclic.com> for CVS use */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Some file systems are case-insensitive.  If FOLD_FN_CHAR is
   #defined, it maps the character C onto its "canonical" form.  In a
   case-insensitive system, it would map all alphanumeric characters
   to lower case.  Under Windows NT, / and \ are both path component
   separators, so FOLD_FN_CHAR would map them both to /.  */
#ifndef FOLD_FN_CHAR
#define FOLD_FN_CHAR(c) toupper(c)
#endif

/* IGNORE(@ */
/* #include <ansidecl.h> */
/* @) */
#include <errno.h>
#include <ctype.h>
#include "fnmatch.h"

//#if !defined(__GNU_LIBRARY__) && !defined(STDC_HEADERS)
//extern int errno;
//#endif

/* Match STRING against the filename pattern PATTERN, returning zero if
   it matches, nonzero if not.  */
int
//#if __STDC__
fnmatch(const char *pattern, const char *string, int flags)
/*#else
fnmatch (pattern, string, flags)
    char *pattern;
    char *string;
    int flags;
#endif*/
{
  register const char *p = pattern, *n = string;
  register char c;

  if ((flags & ~__FNM_FLAGS) != 0)
    {
      errno = EINVAL;
      return -1;
    }

  while ((c = *p++) != '\0')
    {
      switch (c)
	{
	case '?':
	  if (*n == '\0')
	    return FNM_NOMATCH;
	  else if ((flags & FNM_PATHNAME) && *n == '\\')
	    return FNM_NOMATCH;
	  else if ((flags & FNM_PERIOD) && *n == '.' &&
		   (n == string || ((flags & FNM_PATHNAME) && n[-1] == '\\')))
	    return FNM_NOMATCH;
	  break;

	case '*':
	  if ((flags & FNM_PERIOD) && *n == '.' &&
	      (n == string || ((flags & FNM_PATHNAME) && n[-1] == '\\')))
	    return FNM_NOMATCH;

	  for (c = *p++; c == '?' || c == '*'; c = *p++, ++n)
	    if (((flags & FNM_PATHNAME) && *n == '\\') ||
		(c == '?' && *n == '\0'))
	      return FNM_NOMATCH;

	  if (c == '\0')
	    return 0;

	  {
	    char c1 = c;
	    for (--p; *n != '\0'; ++n)
	      if ((c == '[' || *n == c1) &&
		  fnmatch(p, n, flags & ~FNM_PERIOD) == 0)
		return 0;
	    return FNM_NOMATCH;
	  }

	case '[':
	  {
	    /* Nonzero if the sense of the character class is inverted.  */
	    register int not_;

	    if (*n == '\0')
	      return FNM_NOMATCH;

	    if ((flags & FNM_PERIOD) && *n == '.' &&
		(n == string || ((flags & FNM_PATHNAME) && n[-1] == '\\')))
	      return FNM_NOMATCH;

		  not_ = (*p == '!' || *p == '^');
	    if (not_)
	      ++p;

	    c = *p++;
	    for (;;)
	      {
		register char cstart = c, cend = c;

		if (c == '\0')
		  /* [ (unterminated) loses.  */
		  return FNM_NOMATCH;

		c = *p++;

		if ((flags & FNM_PATHNAME) && c == '\\')
		  /* [/] can never match.  */
		  return FNM_NOMATCH;

		if (c == '-' && *p != ']')
		  {
		    cend = *p++;
		    if (cend == '\0')
		      return FNM_NOMATCH;
		    c = *p++;
		  }

		if (*n >= cstart && *n <= cend)
		  goto matched;

		if (c == ']')
		  break;
	      }
	    if (!not_)
	      return FNM_NOMATCH;
	    break;

	  matched:;
	    /* Skip the rest of the [...] that already matched.  */
	    while (c != ']')
	      {
		if (c == '\0')
		  /* [... (unterminated) loses.  */
		  return FNM_NOMATCH;

		c = *p++;
	      }
	    if (not_)
	      return FNM_NOMATCH;
	  }
	  break;
	  
	default:
	  if (FOLD_FN_CHAR (c) != FOLD_FN_CHAR (*n))
	    return FNM_NOMATCH;
	}
      
      ++n;
    }

  if (*n == '\0')
    return 0;

  return FNM_NOMATCH;
}
