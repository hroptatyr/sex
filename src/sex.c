/*** sex.c -- simulating executions
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#if defined HAVE_DFP754_H
# include <dfp754.h>
#elif defined HAVE_DFP_STDLIB_H
# include <dfp/stdlib.h>
#elif defined HAVE_DECIMAL_H
# include <decimal.h>
#else
__attribute__((pure, const)) bool
isnand64(_Decimal64 x)
{
	return __builtin_isnand64(x);
}
#endif	/* DFP754_H || HAVE_DFP_STDLIB_H || HAVE_DECIMAL_H */
#include <books/books.h>
#include "dfp754_d64.h"
#include "xquo.h"
#include "hash.h"
#include "nifty.h"

#define strtoqx		strtod64
#define strtopx		strtod64
#define qxtostr		d64tostr
#define pxtostr		d64tostr

#define NANPX		NAND64
#define isnanpx		isnand64

static const char *cont;
static size_t conz;
static hx_t conx;

static qx_t _glob_qty = 1.dd;


static __attribute__((format(printf, 1, 2))) void
serror(const char *fmt, ...)
{
	va_list vap;
	va_start(vap, fmt);
	vfprintf(stderr, fmt, vap);
	va_end(vap);
	if (errno) {
		fputc(':', stderr);
		fputc(' ', stderr);
		fputs(strerror(errno), stderr);
	}
	fputc('\n', stderr);
	return;
}


static void
send_beef(tv_t m, qx_t q, px_t p)
{
	char buf[256U];
	size_t len = 0U;

	len += tvtostr(buf + len, sizeof(buf) - len, m);
	buf[len++] = '\t';
	len += (memcpy(buf + len, "TRA\t", 4U), 4U);
	len += qxtostr(buf + len, sizeof(buf) - len, q);
	buf[len++] = '\t';
	len += qxtostr(buf + len, sizeof(buf) - len, p);
	buf[len++] = '\n';
	fwrite(buf, 1, len, stdout);
	return;
}


static xord_t
yield_ord(FILE *ofp)
{
	static char *line;
	static size_t llen;
	ssize_t nrd;
	xord_t r;
	hx_t h;

retry:
	if (UNLIKELY((nrd = getline(&line, &llen, ofp)) <= 0)) {
		free(line);
		line = NULL;
		llen = 0UL;
		return NOT_A_XORD;
	}
	/* rewind to before possible newline */
	nrd -= line[nrd - 1] == '\n';

	/* use xquo's helper to parse the line */
	r = read_xord(line, nrd);

	/* check order */
	if (UNLIKELY(NOT_A_XORD_P(r))) {
		/* is broken line */
		goto retry;
	}
	/* check instrument */
	if (UNLIKELY(!(h = hash(r.ins, r.inz)) || h != conx && conx)) {
		/* is for us not */
		goto retry;
	}
	/* fill in quantity */
	r.o.qty = r.o.qty ?: _glob_qty;
	return r;
}

static xquo_t
yield_quo(FILE *qfp)
{
	static char *line;
	static size_t llen;
	ssize_t nrd;
	xquo_t r;
	hx_t h;

retry:
	if (UNLIKELY((nrd = getline(&line, &llen, qfp)) <= 0)) {
		free(line);
		line = NULL;
		llen = 0UL;
		return NOT_A_XQUO;
	}

	/* use xquo helper to parse the line */
	r = read_xquo(line, nrd);

	/* check order */
	if (UNLIKELY(NOT_A_XQUO_P(r))) {
		/* is broken line */
		goto retry;
	}
	/* check side */
	if (UNLIKELY(r.o.s == BOOK_SIDE_UNK || r.o.s >= BOOK_SIDE_CLR)) {
		/* is valid side not */
		goto retry;
	}
	/* check instrument */
	if (UNLIKELY(!(h = hash(r.ins, r.inz)) || h != conx && conx)) {
		/* is for us not */
		goto retry;
	}
	return r;
}


#include <assert.h>
static __attribute__((noinline)) int
offline(FILE *qfp)
{
	xord_t _oq[256U], *oq = _oq;
	size_t ioq = 0U, noq = 0U, zoq = countof(_oq);
	book_t b;

	b = make_book();

	/* we can't do nothing before the first quote, so read that one
	 * as a reference and fast forward orders beyond that point */
	for (xquo_t newq; !NOT_A_XQUO_P(newq = yield_quo(qfp));) {
	ord:
		if (UNLIKELY(stdin == NULL)) {
			/* order file is eof'd, skip fetching more */
			goto exe;
		}
		for (xord_t newo;
		     noq < zoq && !NOT_A_XORD_P(newo = yield_ord(stdin));
		     oq[noq++] = newo);
		if (noq < zoq) {
			/* out of orders, shut him up */
			fclose(stdin);
			stdin = NULL;
		}

	exe:
		/* go through order queue and try exec'ing @q */
		for (size_t i = ioq; i < noq && oq[i].o.t < newq.o.t; i++) {
			const ord_t o = oq[i].o;
			book_pdo_t pd = book_pdo(b, o.sid, o.qty, o.lmt);
			if (pd.base > 0.dd) {
				send_beef(o.t, pd.base, pd.term / pd.base);
				/* mark executed */
				oq[i].o.t = NATV;
			}
		}
		/* fast forward dead orders */
		for (; ioq < noq && oq[ioq].o.t == NATV; ioq++);
		/* gc'ing again */
		if (UNLIKELY(ioq >= zoq / 2U)) {
			memmove(oq, oq + ioq, (noq - ioq) * sizeof(*oq));
			noq -= ioq;
			ioq = 0U;
		}
		if (UNLIKELY(stdin == NULL)) {
			/* order file is eof'd, skip fetching more */
			;
		} else if (UNLIKELY(!noq)) {
			/* fill up the queue some more and do more exec'ing */
			goto ord;
		} else if (oq[noq - 1U].o.t < newq.o.t) {
			/* there could be more orders between Q and NEWQ
			 * try exec'ing those as well */
			if (UNLIKELY(noq >= ioq + zoq / 2U)) {
				/* resize :( */
				xord_t *nuq = malloc((zoq *= 16U) * sizeof(*oq));
				memcpy(nuq, oq + ioq, (noq - ioq) * sizeof(*oq));
				noq -= ioq;
				ioq = 0U;
				if (oq != _oq) {
					free(oq);
				}
				oq = nuq;
			}
			goto ord;
		}

		/* at last build up new book */
		book_add(b, newq.o);
	}
	free_book(b);
	return 0;
}


#include "sex.yucc"

int
main(int argc, char *argv[])
{
	static yuck_t argi[1U];
	int rc = 0;
	FILE *qfp;

	if (yuck_parse(argi, argc, argv) < 0) {
		rc = 1;
		goto out;
	} else if (!argi->nargs) {
		errno = 0, serror("\
Error: QUOTES file is mandatory.");
		rc = 1;
		goto out;
	} else if (UNLIKELY((qfp = fopen(*argi->args, "r")) == NULL)) {
		serror("\
Error: cannot open QUOTES file `%s'", *argi->args);
		rc = 1;
		goto out;
	}

	if (argi->pair_arg) {
		cont = argi->pair_arg;
		conz = strlen(cont);
		conx = hash(cont, conz);
	}

	/* read orders from stdin, quotes from QFP and execute */
	rc = offline(qfp) < 0;

	fclose(qfp);
out:
	yuck_free(argi);
	return rc;
}
