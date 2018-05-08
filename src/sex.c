/*** sex.c -- simulating executions
 *
 * Copyright (C) 2016-2018 Sebastian Freundt
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
static inline __attribute__((pure, const)) _Decimal64
fabsd64(_Decimal64 x)
{
	return x >= 0 ? x : -x;
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

#define fabspx		fabsd64
#define fabsqx		fabsd64

typedef struct {
	qx_t q;
	px_t p;
} tra_t;

typedef struct {
	qx_t q;
	px_t p;
	/* spread at the time */
	px_t s;
	/* effective spread */
	px_t e;
	/* youngest liquidity touched */
	tv_t y;
	/* oldest liquidity touched */
	tv_t z;
} exe_t;

typedef struct {
	qx_t base;
	qx_t term;
	qx_t comm;
	qx_t effs;
	tv_t yngt;
	tv_t oldt;
} acc_t;

typedef struct {
	px_t base;
	px_t term;
} com_t;

static const char *cont;
static size_t conz;
static hx_t conx;

static qx_t _glob_qty = 1.dd;
static tv_t _glob_age;
static com_t _glob_com = {0.dd, 0.dd};


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

static inline __attribute__((pure, const)) tv_t
max_tv(tv_t t1, tv_t t2)
{
	return t1 >= t2 ? t1 : t2;
}

static inline __attribute__((pure, const)) book_side_t
contra(book_side_t s)
{
/* ASK to BID and BID to ASK */
	return (book_side_t)(s ^ 0b11U);
}

static inline __attribute__((pure, const)) tra_t
pdo2tra(book_pdo_t pd, book_side_t s)
{
	switch (s) {
	case BOOK_SIDE_ASK:
		return (tra_t){pd.base, pd.term / pd.base};
	case BOOK_SIDE_BID:
		return (tra_t){-pd.base, pd.term / pd.base};
	default:
		break;
	}
	return (tra_t){0.dd, NANPX};
}

static ord_t
ao(ord_t o, acc_t a)
{
	switch (o.sid) {
	case BOOK_SIDE_CLR:
		o.sid = a.base < 0.dd ? BOOK_SIDE_ASK : BOOK_SIDE_BID;
		o.qty = o.qty ?: fabsqx(a.base);
		break;
	default:
		break;
	}
	return o;
}


static void
send_exe(tv_t m, exe_t x)
{
	static const char vexe[] = "EXE\t";
	static const char vrej[] = "REJ\t";
	char buf[256U];
	size_t len = 0U;

	len += tvtostr(buf + len, sizeof(buf) - len, m);
	buf[len++] = '\t';
	len += (memcpy(buf + len, cont, conz), conz);
	buf[len++] = '\t';
	len += (memcpy(buf + len, isnanpx(x.p) ? vrej : vexe, 4U), 4U);
	len += qxtostr(buf + len, sizeof(buf) - len, x.q);
	buf[len++] = '\t';
	len += pxtostr(buf + len, sizeof(buf) - len, x.p);
	/* top spread at the time */
	buf[len++] = '\t';
	len += pxtostr(buf + len, sizeof(buf) - len, x.s);
	/* eff spread at the time */
	buf[len++] = '\t';
	len += pxtostr(buf + len, sizeof(buf) - len, x.e);
	/* youngest touched */
	buf[len++] = '\t';
	len += tvtostr(buf + len, sizeof(buf) - len, x.y);
	/* oldest touched */
	buf[len++] = '\t';
	len += tvtostr(buf + len, sizeof(buf) - len, x.z);
	buf[len++] = '\n';
	fwrite(buf, 1, len, stdout);
	return;
}

static void
send_acc(tv_t m, acc_t a)
{
	static const char verb[] = "ACC\t";
	char buf[256U];
	size_t len;

	len = tvtostr(buf, sizeof(buf), m);
	buf[len++] = '\t';
	len += (memcpy(buf + len, cont, conz), conz);
	buf[len++] = '\t';
	len += (memcpy(buf + len, verb, strlenof(verb)), strlenof(verb));
	len += qxtostr(buf + len, sizeof(buf) - len, a.base);
	buf[len++] = '\t';
	len += qxtostr(buf + len, sizeof(buf) - len, a.term);
	buf[len++] = '\t';
	len += qxtostr(buf + len, sizeof(buf) - len, a.comm);
	buf[len++] = '\t';
	len += qxtostr(buf + len, sizeof(buf) - len, a.effs);
	buf[len++] = '\t';
	len += tvtostr(buf + len, sizeof(buf) - len, a.yngt);
	buf[len++] = '\t';
	len += tvtostr(buf + len, sizeof(buf) - len, a.oldt);
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
	switch (r.o.sid) {
	case BOOK_SIDE_BID:
	case BOOK_SIDE_ASK:
		r.o.qty = r.o.qty ?: _glob_qty;
	default:
		break;
	}
	r.o.t += _glob_age;
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

static acc_t
alloc(acc_t a, exe_t x, com_t c)
{
/* allocate execution X to account A. */
	if (LIKELY(!isnanpx(x.p))) {
		/* calc accounts */
		a.base += x.q;
		a.term -= x.q * x.p;
		a.comm -= fabsqx(x.q) * c.base;
		a.comm -= fabsqx(x.q * x.p) * c.term;
		a.effs -= fabsqx(x.q) * x.e;
		a.yngt += x.y;
		a.oldt += x.z;
	}
	return a;
}


static int
offline(FILE *qfp)
{
	xord_t _oq[256U], *oq = _oq;
	size_t ioq = 0U, noq = 0U, zoq = countof(_oq);
	tv_t metr = 0U;
	acc_t a = {
		.base = 0.dd, .term = 0.dd, .comm = 0.dd, .effs = 0.dd,
	};
	book_t b;

	b = make_book();

	/* we can't do nothing before the first quote, so read that one
	 * as a reference and fast forward orders beyond that point */
	for (xquo_t q; (q = yield_quo(qfp), true); metr = q.o.t) {
	ord:
		if (UNLIKELY(stdin == NULL)) {
			/* order file is eof'd, skip fetching more */
			goto exe;
		}
		for (xord_t o;
		     noq < zoq && !NOT_A_XORD_P(o = yield_ord(stdin));
		     oq[noq++] = o);
		if (noq < zoq) {
			/* out of orders, shut him up */
			fclose(stdin);
			stdin = NULL;
		}

	exe:
		if (NOT_A_XQUO_P(q) && a.base) {
			/* inject a CANCEL order */
			const ord_t o = {
				ORD_MKT, BOOK_SIDE_CLR,
				.qty = 0.dd, .lmt = NANPX, .t = metr
			};
			oq[noq++] = (xord_t){ao(o, a), cont, conz};
			q.o.t = NATV;
		}
		/* go through order queue and try exec'ing @q */
		for (size_t i = ioq; i < noq && oq[i].o.t < q.o.t; i++) {
			const ord_t o = ao(oq[i].o, a);
			book_pdo_t d = book_pdo(b, o.sid, o.qty, o.lmt);

			if (UNLIKELY(d.base <= 0.dd)) {
				continue;
			}

			book_pdo_t c = book_pdo(b, contra(o.sid), o.qty, NANPX);
			book_quo_t topb = book_top(b, BOOK_SIDE_BID);
			book_quo_t topa = book_top(b, BOOK_SIDE_ASK);
			tra_t trad = pdo2tra(d, o.sid);
			tra_t trac = pdo2tra(c, contra(o.sid));
			exe_t x;

			/* copy prices */
			x.p = trad.p;
			x.q = trad.q;

			/* calc spreads */
			x.s = topa.p - topb.p;
			x.e = fabspx(trac.p - trad.p);

			/* calc age */
			metr = max_tv(metr, o.t);
			x.y = d.yngt > 0U ? metr - d.yngt : 0U;
			x.z = d.oldt < NATV ? metr - d.oldt : 0U;

			send_exe(metr, x);
			if (o.qty - d.base <= 0.dd) {
				/* mark executed */
				oq[i].o.t = NATV;
			} else if (oq[i].o.qty > 0.dd) {
				oq[i].o.qty -= d.base;
			}

			/* allocate */
			a = alloc(a, x, _glob_com);
			send_acc(metr, a);
		}
		if (NOT_A_XQUO_P(q)) {
			break;
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
		} else if (oq[noq - 1U].o.t < q.o.t) {
			/* there could be more orders between then and Q
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
		book_add(b, q.o);
		if (q.r.s) {
			book_add(b, q.r);
		}
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
	}

	if (argi->exe_delay_arg) {
		char *on;

		_glob_age = strtotv(argi->exe_delay_arg, &on);
		if (UNLIKELY(_glob_age == NATV)) {
			errno = 0, serror("\
Error: cannot read exe-delay argument");
			rc = 1;
			goto out;
		}
		switch (*on++) {
		secs:
		case '\0':
		case 'S':
		case 's':
			/* seconds but strtotv() gives us nanos already */
			break;

		case 'n':
		case 'N':
			switch (*on) {
			case 's':
			case 'S':
				_glob_age /= NSECS;
				break;
			default:
				goto invalid;
			}
			break;

		case 'm':
		case 'M':
			switch (*on) {
			case '\0':
				/* they want minutes, oh oh */
				_glob_age *= 60UL;
				goto secs;
			case 's':
			case 'S':
				/* milliseconds it is then */
				_glob_age /= MSECS;
				break;
			default:
				goto invalid;
			}
			break;

		case 'h':
		case 'H':
			_glob_age *= 60U * 60U;
			goto secs;

		case 'u':
		case 'U':
			switch (*on) {
			case 's':
			case 'S':
				on++;
				_glob_age /= USECS;
				break;
			default:
				goto invalid;
			}
			break;

		default:
		invalid:
			errno = 0, serror("\
Error: invalid suffix to exe-dealy, must be `s', `ms', `us', `ns'");
			rc = 1;
			goto out;
		}
	}

	if (argi->commission_arg) {
		char *on = argi->commission_arg;

		switch (*on) {
		default:
			_glob_com.base = strtopx(on, &on);

			if (*on == '/') {
		case '/':
			_glob_com.term = strtopx(++on, &on);
			if (*on == '/') {
				errno = 0, serror("\
Error: commission must be given as PXb[/PXt]");
				rc = 1;
				goto out;
			}
			}
			break;
		}
	}

	if (argi->quantity_arg) {
		_glob_qty = strtoqx(argi->quantity_arg, NULL);
	}

	if (UNLIKELY((qfp = fopen(*argi->args, "r")) == NULL)) {
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
