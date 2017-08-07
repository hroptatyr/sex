/* memrchr -- find the last occurrence of a byte in a memory block

   Copyright (C) 1991, 1993, 1996-1997, 1999-2000, 2003-2017 Free Software
   Foundation, Inc.

   Based on strlen implementation by Torbjorn Granlund (tege@sics.se),
   with help from Dan Sahlin (dan@sics.se) and
   commentary by Jim Blandy (jimb@ai.mit.edu);
   adaptation to memchr suggested by Dick Karpinski (dick@cca.ucsf.edu),
   and implemented by Roland McGrath (roland@ai.mit.edu).

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */
#if !defined INCLUDED_memrchr_h_
#define INCLUDED_memrchr_h_
#include <unistd.h>

extern void *memrchr(void const *s, int c_in, size_t n);

#endif	/* INCLUDED_memrchr_h_ */
