/*
 * Copyright (c) 1997, 1998, 1999
 *      Inferno Nettverk A/S, Norway.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. The above copyright notice, this list of conditions and the following
 *    disclaimer must appear in all copies of the software, derivative works
 *    or modified versions, and any portions thereof, aswell as in all
 *    supporting documentation.
 * 2. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by
 *      Inferno Nettverk A/S, Norway.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Inferno Nettverk A/S requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  sdc@inet.no
 *  Inferno Nettverk A/S
 *  Oslo Research Park
 *  Gaustadal�en 21
 *  N-0349 Oslo
 *  Norway
 *
 * any improvements or extensions that they make and grant Inferno Nettverk A/S
 * the rights to redistribute these changes.
 *
 */

#include "common.h"

static const char rcsid[] =
"$Id: sockd_negotiate.c,v 1.56 1999/05/13 17:06:31 michaels Exp $";

__BEGIN_DECLS

static int
send_negotiate __P((const struct sockd_mother_t *mother,
						  const struct sockd_negotiate_t *neg));
/*
 * Sends "neg" to "s".  Also ack's that we have freed a slot to "s".
 * Returns:
 *		On success: 0
 *		On failure: -1
 *		If some other problem prevented success: > 0
*/


static int
recv_negotiate __P((const struct sockd_mother_t *mother));
/*
 * Tries to receive a client from mother "s".
 * Returns:
 *		On success: 0
 *		If a error happened to connection with "s": -1
 *		If some other problem prevented success: > 0
*/

static void
delete_negotiate __P((const struct sockd_mother_t *mother,
							 struct sockd_negotiate_t *neg));
/*
 * Frees any state occupied by "neg", including closing any
 * descriptors and sending a ack that we have deleted a "negotiate"
 * object to "mother".
*/


static int
neg_fillset __P((fd_set *set));
/*
 * Sets all descriptors in our list in the set "set".
 * Returns the highest descriptor in our list, or -1 if we don't
 * have any descriptors open currently.
*/

static void
neg_clearset __P((struct sockd_negotiate_t *neg, fd_set *set));
/*
 * Clears all filedescriptors in "neg" from "set".
*/


static struct sockd_negotiate_t *
neg_getset __P((fd_set *set));
/*
 * Goes through our list until it finds a negotiate object where atleast
 * one of the descriptors is set.
 * Returns:
 *		On success: pointer to the found object.
 *		On failure: NULL.
*/

static int
allocated __P((void));
/*
 * Returns the number of allocated (active) objects.
*/

static int
completed __P((void));
/*
 * Returns the number of objects completed and ready to be sent currently.
*/

static int
allocated __P((void));
/*
 * Returns the number of objects currently allocated for use.
*/

static void
proctitleupdate __P((void));
/*
 * Updates the title of this process.
*/

static struct timeval *
neg_gettimeout __P((struct timeval *timeout));
/*
 * Fills in "timeout" with time til the first clients connection
 * expires.
 * Returns:
 *		If there is a timeout: pointer to filled in "timeout".
 *		If there is no timeout: NULL.
*/

static struct sockd_negotiate_t *
neg_gettimedout __P((void));
/*
 * Scans all clients for one that has timed out according to config
 * settings.
 * Returns:
 *		If timed out client found: pointer to it.
 *		Else: NULL.
*/

__END_DECLS

static struct sockd_negotiate_t negv[SOCKD_NEGOTIATEMAX];
static int negc = ELEMENTS(negv);


void
run_negotiate(mother)
	struct sockd_mother_t *mother;
{

	proctitleupdate();

	/* CONSTCOND */
	while (1) {
		fd_set rset, wsetmem, *wset = NULL;
		int fdbits, p;
		struct sockd_negotiate_t *neg;
		struct timeval timeout;

		fdbits = neg_fillset(&rset);
		FD_SET(mother->s, &rset);
		fdbits = MAX(fdbits, mother->s);

		/* if we have a completed request check whether we can send to mother. */
		if (completed() > 0) {
			FD_ZERO(&wsetmem);
			FD_SET(mother->s, &wsetmem);
			wset = &wsetmem;
		}

		++fdbits;
		switch (select(fdbits, &rset, wset, NULL, neg_gettimeout(&timeout))) {
			case -1:
				SERR(-1);
				/* NOTREACHED */

			case 0: {
				const char *reason = "negotiation timed out";

				if ((neg = neg_gettimedout()) == NULL)
					continue; /* should only be possible if sighup received. */

				iolog(&neg->rule, &neg->state, OPERATION_ABORT, &neg->src,
				&neg->dst, reason, strlen(reason));

				delete_negotiate(mother, neg);

				continue;
			}
		}

		if (FD_ISSET(mother->s, &rset)) {
			if (recv_negotiate(mother) == -1)
				sockdexit(-EXIT_FAILURE);
			FD_CLR(mother->s, &rset);
		}

		while ((neg = neg_getset(&rset)) != NULL) {
			neg_clearset(neg, &rset);

			if ((p = recv_request(neg->s, &neg->req, &neg->negstate)) <= 0) {
				const char *reason = NULL;	/* init or gcc complains. */

				switch (p) {
					case 0:
						reason = "eof from client";
						break;

					case -1:
						switch (errno) {
							case 0:
								reason = "socks protocol error";
								break;

							case EINTR:
							case EAGAIN:
#if EAGAIN != EWOULDBLOCK
							case EWOULDBLOCK:
#endif
								continue; /* ok, retry. */

							default:
								reason = strerror(errno);
						}
				}

				iolog(&neg->rule, &neg->state, OPERATION_ABORT, &neg->src,
				&neg->dst, reason, strlen(reason));

				delete_negotiate(mother, neg);
			}
			else if (wset != NULL && FD_ISSET(mother->s, wset)) {
				/* read a complete request, send to mother. */
				switch (send_negotiate(mother, neg)) {
					case -1:
						sockdexit(-EXIT_FAILURE);
						/* NOTREACHED */

					case 0:
						delete_negotiate(mother, neg); /* sent to mother ok. */
						break;
				}
			}
		}
	}
}


static int
send_negotiate(mother, neg)
	const struct sockd_mother_t *mother;
	const struct sockd_negotiate_t *neg;
{
	const char *function = "send_negotiate()";
	struct iovec iovec[1];
	struct sockd_request_t req;
	int fdsendt, w;
	struct msghdr msg;
	CMSG_AALLOC(sizeof(int));

#if HAVE_SENDMSG_DEADLOCK
	if (socks_lock(mother->lock, F_WRLCK, 0) != 0)
		return 1;
#endif /* HAVE_SENDMSG_DEADLOCK */

	/* copy needed fields from negotiate */
	req.req	= neg->req;
	req.auth	= neg->auth;
	req.rule = neg->rule;
	/* LINTED pointer casts may be troublesome */
	sockshost2sockaddr(&neg->src, (struct sockaddr *)&req.from);
	/* LINTED pointer casts may be troublesome */
	sockshost2sockaddr(&neg->dst, (struct sockaddr *)&req.to);

	iovec[0].iov_base		= &req;
	iovec[0].iov_len		= sizeof(req);

	fdsendt = 0;
	CMSG_ADDOBJECT(neg->s, sizeof(neg->s) * fdsendt++);

	msg.msg_iov				= iovec;
	msg.msg_iovlen			= ELEMENTS(iovec);
	msg.msg_name			= NULL;
	msg.msg_namelen		= 0;

	CMSG_SETHDR_SEND(sizeof(int) * fdsendt);

	slog(LOG_DEBUG, "sending request to mother");
	if ((w = sendmsg(mother->s, &msg, 0)) != sizeof(req))
		switch (errno) {
			case EAGAIN:
			case ENOBUFS:
				w = 1;	/* temporal error. */
				break;

			default:
				swarn("%s: sendmsg(): %d of %d", function, w, sizeof(req));
		}

#if HAVE_SENDMSG_DEADLOCK
	if (socks_unlock(mother->lock, -1) != 0)
		SERR(errno);
#endif /* HAVE_SENDMSG_DEADLOCK */

	return w == sizeof(req) ? 0 : w;
}


static int
recv_negotiate(mother)
	const struct sockd_mother_t *mother;
{
	const char *function = "recv_negotiate()";
	struct sockd_negotiate_t *neg;
	struct iovec iovec[1];
	struct sockaddr addr;
	socklen_t len;
	unsigned char command;
	int permit, i, r, fdexpect, fdreceived;
	struct msghdr msg;
	CMSG_AALLOC(sizeof(int));


	iovec[0].iov_base		= &command;
	iovec[0].iov_len		= sizeof(command);

	msg.msg_iov				= iovec;
	msg.msg_iovlen			= ELEMENTS(iovec);
	msg.msg_name			= NULL;
	msg.msg_namelen		= 0;

	CMSG_SETHDR_RECV(sizeof(cmsgmem));

	if ((r = recvmsgn(mother->s, &msg, 0, sizeof(command))) != sizeof(command)) {
		switch (r) {
			case -1:
				swarn("%s: recvmsg() from mother", function);
				break;

			case 0:
				slog(LOG_DEBUG, "%s: recvmsg(): mother closed connection",
				function);
				break;

			default:
				swarnx("%s: recvmsg(): unexpected %d/%d bytes from mother",
				function, r, sizeof(command));
		}

		return -1;
	}
	fdexpect = 1;	/* constant */

	SASSERTX(command == SOCKD_NEWREQUEST);

	/* find a free slot. */
	for (i = 0, neg = NULL; i < negc; ++i)
		if (!negv[i].allocated) {
			neg = &negv[i];
			break;
		}

	if (neg == NULL)
		SERRX(allocated());

#if !HAVE_DEFECT_RECVMSG
	SASSERT(CMSG_GETLEN(msg) == sizeof(int) * fdexpect);
#endif

	fdreceived = 0;
	CMSG_GETOBJECT(neg->s, sizeof(neg->s) * fdreceived++);

	/* get local and remote address. */

	len = sizeof(addr);
	if (getpeername(neg->s, &addr, &len) != 0) {
		swarn("%s: getpeername()", function);
		return 1;
	}
	sockaddr2sockshost(&addr, &neg->src);

	len = sizeof(addr);
	if (getsockname(neg->s, &addr, &len) != 0) {
		swarn("%s: getsockname()", function);
		return 1;
	}
	sockaddr2sockshost(&addr, &neg->dst);

	neg->state.command		= SOCKS_ACCEPT;
	neg->state.protocol		= SOCKS_TCP;
	neg->state.auth.method	= AUTHMETHOD_NONE;
	/* pointer fixup */
	neg->req.auth = &neg->auth;
	neg->allocated = 1;

	permit = clientaddressisok(neg->s, &neg->src, &neg->dst, neg->state.protocol,
	&neg->rule);

	iolog(&neg->rule, &neg->state, OPERATION_ACCEPT, &neg->src, &neg->dst,
	NULL, 0);

	if (!permit) {
		delete_negotiate(mother, neg);
		return 0;
	}

	time(&neg->start);

	proctitleupdate();

	return 0;
}

static void
delete_negotiate(mother, neg)
	const struct sockd_mother_t *mother;
	struct sockd_negotiate_t *neg;
{
	const char *function = "delete_negotiate()";
	static const struct sockd_negotiate_t neginit;
	const char command = SOCKD_FREESLOT;

	SASSERTX(neg->allocated);

	close(neg->s);

	*neg = neginit;

	/* ack we have freed a slot. */
	if (writen(mother->ack, &command, sizeof(command)) != sizeof(command))
		swarn("%s: writen()", function);

	proctitleupdate();
}


static int
neg_fillset(set)
	fd_set *set;
{
	int i, max;

	FD_ZERO(set);

	for (i = 0, max = -1; i < negc; ++i)
		if (negv[i].allocated) {
			negv[i].ignore = 0;
			FD_SET(negv[i].s, set);
			max = MAX(max, negv[i].s);
		}

	return max;
}

static void
neg_clearset(neg, set)
	struct sockd_negotiate_t *neg;
	fd_set *set;
{

	FD_CLR(neg->s, set);
	neg->ignore = 1;
}


static struct sockd_negotiate_t *
neg_getset(set)
	fd_set *set;
{
	int i;

	for (i = 0; i < negc; ++i)
		if (negv[i].allocated) {
			if (negv[i].ignore)
				continue;

			if (negv[i].negstate.complete)
				return &negv[i];

			if (FD_ISSET(negv[i].s, set))
				return &negv[i];

		}

	return NULL;
}

static int
allocated(void)
{
	int i, alloc;

	for (i = 0, alloc = 0; i < negc; ++i)
		if (negv[i].allocated)
			++alloc;

	return alloc;
}

static int
completed(void)
{
	int i, completec;

	for (i = 0, completec = 0; i < negc; ++i)
		if (negv[i].allocated && negv[i].negstate.complete)
			++completec;

	return completec;
}


static void
proctitleupdate(void)
{

	setproctitle("negotiator: %d/%d", allocated(), SOCKD_NEGOTIATEMAX);
}

static struct timeval *
neg_gettimeout(timeout)
	struct timeval *timeout;
{
	time_t timenow;
	int i;

	if (config.timeout.negotiate == 0 || (allocated() == completed()))
		return NULL;

	timeout->tv_sec	= config.timeout.negotiate;
	timeout->tv_usec	= 0;
	time(&timenow);

	for (i = 0; i < negc; ++i)
		if (!negv[i].allocated)
			continue;
		else
			timeout->tv_sec = MAX(0, MIN(timeout->tv_sec,
			config.timeout.negotiate - (timenow - negv[i].start)));

	return timeout;
}

static struct sockd_negotiate_t *
neg_gettimedout(void)
{
	int i;
	time_t timenow;

	if (config.timeout.negotiate == 0)
		return NULL;

	time(&timenow);
	for (i = 0; i < negc; ++i) {
		if (!negv[i].allocated)
			continue;
		if (negv[i].ignore)
			continue;
		else
			if (timenow - negv[i].start >= config.timeout.negotiate)
				return &negv[i];
	}

	return NULL;
}