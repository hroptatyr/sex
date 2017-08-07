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
#include <errno.h>
#if defined HAVE_DFP754_H
# include <dfp754.h>
#elif defined HAVE_DFP_STDLIB_H
# include <dfp/stdlib.h>
#elif defined HAVE_DECIMAL_H
# include <decimal.h>
#endif	/* DFP754_H || HAVE_DFP_STDLIB_H || HAVE_DECIMAL_H */
#include "dfp754_d64.h"
#include "clob/clob.h"
#include "clob/btree.h"
#include "clob/unxs.h"
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


static clob_ord_t
push_beef(const char *ln, size_t lz)
{
}

static void
send_beef(unxs_exe_t x)
{
	char buf[256U];
	size_t len = (memcpy(buf, "TRA\t", 4U), 4U);

	len += qxtostr(buf + len, sizeof(buf) - len, x.qty);
	buf[len++] = '\t';
	len += qxtostr(buf + len, sizeof(buf) - len, x.prc);
	buf[len++] = '\n';
	fwrite(buf, 1, len, stdout);
	return;
}

static void
send_cake(clob_t c, unxs_exe_t x)
{
	char buf[256U];
	size_t len = 0U;

	if (UNLIKELY(isnanpx(x.prc))) {
		/* don't print nans */
		return;
	}
	len = pxtostr(buf + len, sizeof(buf) - len, x.prc);
	buf[len++] = '\n';
	//fwrite(buf, 1, len, quoout);
	return;
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

	/* check timestamp */
	if (UNLIKELY(r.t == NANTV)) {
		/* is broken line */
		goto retry;
	}
	/* check side */
	if (UNLIKELY(r.s == SIDE_UNK || r.s >= SIDE_CLR)) {
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
static int
offline(FILE *qfp)
{
	xquo_t q = NOT_A_XQUO;
	clob_t c;

	c = make_clob();
	c.exe = make_unxs(MODE_BI);

	/* we can't do nothing before the first quote, so read that one
	 * as a reference and fast forward orders beyond that point */
	for (xquo_t newq; !NOT_A_XQUO_P(newq = yield_quo(qfp)); q = newq) {
		static clob_oid_t l1[NCLOB_SIDES];
		clob_side_t s;

	ord:
		if (UNLIKELY(stdin == NULL)) {
			/* order file is eof'd, skip fetching more */
			goto exe;
		}

	exe:
		/* print trades at the very least */
		for (size_t i = 0U; i < c.exe->n; i++) {
			send_beef(c.exe->x[i]);
		}
		unxs_clr(c.exe);

		s = (clob_side_t)(newq.s - 1U);
		switch (newq.f) {
			clob_ord_t o;
			int rc;

		case LVL_1:
			if (LIKELY(l1[s].qid > 0)) {
				clob_del(c, l1[s]);
			}
			if (isnanpx(newq.p)) {
				break;
			}
			o = (clob_ord_t){CLOB_TYPE_LMT, s, {newq.q, 0.dd}, newq.p};
			l1[s] = unxs_order(c, o, NANPX);
			break;

		case LVL_2:
			break;

		case LVL_3:
			break;

		case LVL_0:
		default:
			break;
		}
		clob_prnt(c);
	}
	free_unxs(c.exe);
	free_clob(c);
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

clo:
	fclose(qfp);
out:
	yuck_free(argi);
	return rc;
}
