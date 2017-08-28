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
#if defined HAVE_DFP754_H
# include <dfp754.h>
#elif defined HAVE_DFP_STDLIB_H
# include <dfp/stdlib.h>
#elif defined HAVE_DECIMAL_H
# include <decimal.h>
#endif	/* DFP754_H || HAVE_DFP_STDLIB_H || HAVE_DECIMAL_H */
#include "dfp754_d64.h"
#include "xquo.h"
#include "memrchr.h"
#include "nifty.h"

#define strtopx		strtod64
#define pxtostr		d64tostr
#define strtoqx		strtod64
#define qxtostr		d64tostr

#define NANPX		NAND64
#define isnanpx		isnand64

#define NSECS	(1000000000)
#define USECS	(1000000)
#define MSECS	(1000)

#include "memrchr.c"


tv_t
strtotv(const char *ln, char **endptr)
{
	char *on;
	tv_t r;

	/* time value up first */
	with (long unsigned int s, x) {
		if (UNLIKELY(!(s = strtoul(ln, &on, 10)) || on == NULL)) {
			r = NANTV;
			goto out;
		} else if (*on == '.') {
			char *moron;

			x = strtoul(++on, &moron, 10);
			if (UNLIKELY(moron - on > 9U)) {
				return NANTV;
			} else if ((moron - on) % 3U) {
				/* huh? */
				return NANTV;
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
	char *lp, *on;
	xquo_t q;

	/* get timestamp */
	q.t = strtotv(line, NULL);

	/* get qty */
	if (UNLIKELY((lp = memrchr(line, '\t', llen)) == NULL)) {
		/* can't do without quantity */
		return NOT_A_XQUO;
	}
	llen = lp - line;
	q.q = strtoqx(lp + 1U, NULL);

	/* get prc */
	if (UNLIKELY((lp = memrchr(line, '\t', llen)) == NULL)) {
		/* can't do without price */
		return NOT_A_XQUO;
	}
	llen = lp - line;
	q.p = strtopx(lp + 1U, &on);
	if (UNLIKELY(on <= lp + 1U)) {
		/* invalidate price */
		q.p = NANPX;
	}

	/* get flavour, should be just before ON */
	with (unsigned char f = *(unsigned char*)--lp) {
		/* map 1, 2, 3 to LVL_{1,2,3}
		 * everything else goes to LVL_0 */
		f ^= '0';
		q.f = (typeof(q.f))(f & -(f < 4U));
	}

	/* rewind manually */
	for (; lp > line && lp[-1] != '\t'; lp--);
	with (unsigned char s = *(unsigned char*)lp) {
		/* map A or a to ASK and B or b to BID
		 * map C to CLR, D to DEL (and T for TRA to DEL)
		 * everything else goes to SIDE_UNK */
		s &= ~0x20U;
		s &= (unsigned char)-(s ^ '@' < NSIDES || s == 'T');
		s &= 0xfU;
		q.s = (typeof(q.s))s;

		if (UNLIKELY(!q.s)) {
			/* cannot put entry to either side, just ignore */
			return NOT_A_XQUO;
		}
	}

	if (LIKELY(lp-- > line)) {
		llen = lp - line;
		if (LIKELY((q.ins = memrchr(line, '\t', llen)) != NULL)) {
			q.ins++;
		} else {
			q.ins = line;
		}
		q.inz = lp - q.ins;
	} else {
		q.ins = line, q.inz = 0U;
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
	o.t = strtotv(ln, &on);
	if (*on++ != '\t') {
		goto bork;
	}

	switch (*on) {
	case 'B'/*UY*/:
	case 'L'/*ONG*/:
	case 'b'/*uy*/:
	case 'l'/*ong*/:
		o.o.sid = CLOB_SIDE_BID;
		break;
	case 'S'/*ELL|HORT*/:
	case 's'/*ell|hort*/:
		o.o.sid = CLOB_SIDE_ASK;
		break;
	default:
		goto bork;
	}
	if (UNLIKELY((on = memchr(on, '\t', ep - on)) == NULL)) {
		goto bork;
	}
	/* keep track of instrument */
	o.ins = ++on;
	o.inz = ep - on;

	/* everything else is optional */
	if ((on = memchr(on, '\t', ep - on)) == NULL) {
		o.o.qty = qty0;
		o.o.lmt = NANPX;
		o.o.typ = CLOB_TYPE_MKT;
		goto fin;
	}
	/* adapt instrument */
	o.inz = on++ - o.ins;

	/* read quantity */
	o.o.qty.hid = 0.dd;
	o.o.qty.dis = strtoqx(on, &on);
	if (LIKELY(*on > ' ')) {
		/* nope */
		goto bork;
	} else if (*on++ == '\t') {
		o.o.lmt = strtopx(on, &on);
		if (*on > ' ') {
			goto bork;
		}
		o.o.typ = CLOB_TYPE_LMT;
	} else {
		o.o.typ = CLOB_TYPE_MKT;
	}
fin:
	return o;
bork:
	return NOT_A_XORD;
}

/* xquo.c ends here */
