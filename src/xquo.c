/*** xquo.c -- quotes serialising/deserialising
 *
 * Copyright (C) 2016-2017 Sebastian Freundt
 *
 * Author:  Sebastian Freundt <freundt@ga-group.nl>
 *
 * This file is part of sex.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the author nor the names of any contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 **/
#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <string.h>
#if defined HAVE_DFP754_H
# include <dfp754.h>
#elif defined HAVE_DFP_STDLIB_H
# include <dfp/stdlib.h>
#elif defined HAVE_DECIMAL_H
# include <decimal.h>
#endif	/* DFP754_H || HAVE_DFP_STDLIB_H || HAVE_DECIMAL_H */
#if defined BOOKSD32
#include "dfp754_d32.h"
#endif
#include "dfp754_d64.h"
#include "xquo.h"
#include "nifty.h"

#if defined BOOKSD32
#define strtopx		strtod32
#define pxtostr		d32tostr
#else
#define strtopx		strtod64
#define pxtostr		d64tostr
#endif
#define strtoqx		strtod64
#define qxtostr		d64tostr

#define NSECS	(1000000000)
#define USECS	(1000000)
#define MSECS	(1000)


static long unsigned int
strtolu(const char *str, char **endptr)
{
	long unsigned int r = 0U;
	size_t si;

	for (si = 0U; (unsigned char)(str[si] ^ '0') < 10U; si++) {
		r *= 10U;
		r += (unsigned char)(str[si] ^ '0');
	}
	if (endptr) {
		*endptr = (char*)str + si;
	}
	return r;
}


tv_t
strtotv(const char *ln, char **endptr)
{
	char *on;
	tv_t r;

	/* time value up first */
	with (long unsigned int s, x) {
		if (UNLIKELY((s = strtolu(ln, &on), on) == NULL)) {
			r = NATV;
			goto out;
		} else if (*on == '.') {
			char *moron;

			x = strtolu(++on, &moron);
			if (UNLIKELY(moron - on > 9U)) {
				return NATV;
			} else if ((moron - on) % 3U) {
				/* huh? */
				return NATV;
			}
			switch (moron - on) {
			default:
			case 0U:
				x *= MSECS;
			case 3U:
				x *= MSECS;
			case 6U:
				x *= MSECS;
			case 9U:
				/* all is good */
				break;
			}
			on = moron;
		} else {
			x = 0U;
		}
		r = s * NSECS + x;
	}
out:
	if (LIKELY(endptr != NULL)) {
		*endptr = on;
	}
	return r;
}

ssize_t
tvtostr(char *restrict buf, size_t bsz, tv_t t)
{
	long unsigned int ts = t / NSECS;
	long unsigned int tn = t % NSECS;
	size_t i;

	if (UNLIKELY(bsz < 19U)) {
		return 0U;
	}

	buf[0U] = '0';
	for (i = !ts; ts > 0U; ts /= 10U, i++) {
		buf[i] = (ts % 10U) ^ '0';
	}
	/* revert buffer */
	for (char *bp = buf + i - 1, *ap = buf, tmp; bp > ap;
	     tmp = *bp, *bp-- = *ap, *ap++ = tmp);
	/* nanoseconds, fixed size */
	buf[i] = '.';
	for (size_t j = 9U; j > 0U; tn /= 10U, j--) {
		buf[i + j] = (tn % 10U) ^ '0';
	}
	return i + 10U;
}

xquo_t
read_xquo(const char *line, size_t llen)
{
/* process one line */
	const char *const eol = line + llen;
	char *on;
	xquo_t q = {.r = {}};

	/* get timestamp */
	if (UNLIKELY((q.o.t = strtotv(line, &on)) == NATV)) {
		return NOT_A_XQUO;
	} else if (UNLIKELY(*on++ != '\t')) {
		return NOT_A_XQUO;
	}
	/* get instrument */
	q.ins = on;
	if (UNLIKELY((on = memchr(q.ins, '\t', eol - q.ins)) == NULL)) {
		return NOT_A_XQUO;
	}
	q.inz = on++ - q.ins;

	/* side and flavour */
	with (unsigned char s = *on++) {
		/* map A or a to ASK and B or b to BID
		 * map C to CLR, D to DEL (and T for TRA to DEL)
		 * everything else goes to SIDE_UNK */
		s &= ~0x20U;
		s &= (unsigned char)-(s ^ '@' < NBOOK_SIDES || s == 'T');
		s &= 0xfU;
		q.o.s = (typeof(q.o.s))s;

		if (UNLIKELY(!q.o.s)) {
			/* cannot put entry to either side, just ignore */
			return NOT_A_XQUO;
		}
	}
	/* get flavour, should be just before ON */
	with (unsigned char f = *on++) {
		/* map 1, 2, 3 to LVL_{1,2,3}
		 * everything else goes to LVL_0 */
		f ^= '0';
		q.o.f = (typeof(q.o.f))(f & -(f < 4U));
	}

	if (UNLIKELY(*on++ != '\t')) {
		return NOT_A_XQUO;
	}

	/* price and qty is optional now */
	with (const char *p = on) {
		q.o.p = strtopx(p, &on);
		if (UNLIKELY(p >= on || *on != '\t')) {
			q.o.p = NANPX;
			if (UNLIKELY(*on != '\t')) {
				if ((on = memchr(on, '\t', eol - on)) == NULL) {
					return q;
				}
			}
		}
		on++;
	}
	/* get qty */
	with (const char *p = on) {
		q.o.q = strtoqx(p, &on);
		if (UNLIKELY(p >= on)) {
			q.o.q = NANPX;
		}
		on++;
	}

	if (q.o.s == BOOK_SIDE_CLR && q.o.f > BOOK_LVL_0) {
		if (UNLIKELY(q.o.f != BOOK_LVL_1)) {
			return NOT_A_XQUO;
		}
		/* we're on a c1 line */
		q.r.f = q.o.f;
		q.r.t = q.o.t;
		q.r.p = (px_t)q.o.q;
		q.r.s = BOOK_SIDE_ASK;
		q.o.s = BOOK_SIDE_BID;

		q.o.q = strtoqx(on, &on);
		on += *on == '\t';
		q.r.q = strtoqx(on, &on);
	}
	return q;
}

xord_t
read_xord(const char *ln, size_t lz)
{
/* process one line */
	const char *const ep = ln + lz;
	char *on;
	xord_t o;

	if (UNLIKELY(!lz)) {
		goto bork;
	}

	/* get timestamp */
	o.o.t = strtotv(ln, &on);
	if (*on++ != '\t' || o.o.t == NATV) {
		goto bork;
	}

	/* encode which book side we want PDO'd */
	switch (*on) {
	case 'B'/*UY*/:
	case 'L'/*ONG*/:
	case 'b'/*uy*/:
	case 'l'/*ong*/:
		o.o.sid = BOOK_SIDE_ASK;
		break;
	case 'S'/*ELL|HORT*/:
	case 's'/*ell|hort*/:
		o.o.sid = BOOK_SIDE_BID;
		break;
	case 'C'/*ANCEL*/:
	case 'c'/*ancel*/:
		o.o.sid = BOOK_SIDE_CLR;
		break;
	default:
		goto bork;
	}
	if (UNLIKELY((on = memchr(on, '\t', ep - on)) == NULL)) {
		o.ins = NULL, o.inz = 0U;
		goto dflt;
	}
	/* keep track of instrument */
	o.ins = ++on;
	o.inz = ep - on;

	/* everything else is optional */
	if ((on = memchr(on, '\t', ep - on)) == NULL) {
	dflt:
		o.o.qty = 0.dd;
		o.o.lmt = NANPX;
		o.o.typ = ORD_MKT;
		goto fin;
	}
	/* adapt instrument */
	o.inz = on++ - o.ins;

	/* read quantity */
	o.o.qty = strtoqx(on, &on);
	if (LIKELY(*on > ' ')) {
		/* nope */
		goto bork;
	} else if (*on++ == '\t') {
		o.o.lmt = strtopx(on, &on);
		if (*on > ' ') {
			goto bork;
		}
		o.o.typ = ORD_LMT;
	} else {
		o.o.lmt = NANPX;
		o.o.typ = ORD_MKT;
	}
fin:
	return o;
bork:
	return NOT_A_XORD;
}

/* xquo.c ends here */
