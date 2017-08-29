/*** xquo.h -- quote serialising/deserialising
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
#if !defined INCLUDED_xquo_h_
#define INCLUDED_xquo_h_
#include <unistd.h>
#include <books/books.h>

#define NATV	((tv_t)-1ULL)

typedef struct {
	book_quo_t o;

	const char *ins;
	size_t inz;
} xquo_t;

#define NOT_A_XQUO	((xquo_t){NOT_A_QUO})
#define NOT_A_XQUO_P(x)	(NOT_A_QUO_P((x).o))

typedef struct {
	enum {
		ORD_LMT,
		ORD_MKT,
	} typ;
	book_side_t sid;
	qx_t qty;
	union {
		/* limit price for limit orders */
		px_t lmt;
		/* max slippage for market orders */
		px_t slp;
	};
	tv_t t;
} ord_t;

#define NOT_A_ORD	((ord_t){.sid = BOOK_SIDE_UNK})
#define NOT_A_ORD_P(x)	((x).sid == BOOK_SIDE_UNK)

typedef struct {
	ord_t o;

	const char *ins;
	size_t inz;
} xord_t;

#define NOT_A_XORD	((xord_t){NOT_A_ORD})
#define NOT_A_XORD_P(x)	(NOT_A_ORD_P((x).o))


extern tv_t strtotv(const char *ln, char **endptr);
extern ssize_t tvtostr(char *restrict buf, size_t bsz, tv_t t);
extern xquo_t read_xquo(const char *line, size_t llen);
extern xord_t read_xord(const char *line, size_t llen);

#endif	/* INCLUDED_xquo_h_ */
