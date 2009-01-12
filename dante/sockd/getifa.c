/*
 * $Id: getifa.c,v 1.46 2009/01/12 18:58:17 karls Exp $
 *
 * Copyright (c) 2001, 2002, 2003, 2006, 2009
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
 *  Gaustadall�en 21
 *  NO-0349 Oslo
 *  Norway
 *
 * any improvements or extensions that they make and grant Inferno Nettverk A/S
 * the rights to redistribute these changes.
 *
 */

/*
 * The Linux code originated from Tom Chan <tchan@austin.rr.com>.
 * The BSD code came from Christoph Badura <bad@bsd.de>.
 *
 * Thanks, guys.
 */

#include "common.h"

static const char rcsid[] =
"$Id: getifa.c,v 1.46 2009/01/12 18:58:17 karls Exp $";


/*
 * Given a destination address, getifa() returns the local source address
 * that will be selected by the OS to connect to that destination address.
 */
#if HAVE_NET_IF_DL_H
#include   <net/if_dl.h>
#endif /* HAVE_NET_IF_DL_H */
#include   <net/route.h>           /* RTA_xxx constants */
#if HAVE_ROUTEINFO_LINUX
#include   <asm/types.h>
#include   <linux/netlink.h>
#include   <linux/rtnetlink.h>
#endif /* HAVE_ROUTEINFO_LINUX */

__BEGIN_DECLS

static struct in_addr
getdefaultexternal __P((void));
/*
 * Returns the default IP address to use for external connections.
 */

static int
isonexternal __P((const struct sockaddr *addr));
/*
 * Returns true if "addr" is configured for the external interface,
 * otherwise false.
 */

__END_DECLS

#if HAVE_ROUTEINFO_LINUX
typedef unsigned char uchar_t;

struct in_addr
getifa(destaddr)
   struct in_addr destaddr;
{
   const char *function = "getifa()";
   struct {
      struct nlmsghdr nh;
      struct rtmsg   rt;
      char           attrbuf[512];
   } req;
   struct rtattr *rta;
   char buf[BUFSIZ], a[MAXSOCKADDRSTRING];
   struct nlmsghdr *rhdr;
   struct rtmsg *rrt;
   struct rtattr *rrta;
   struct sockaddr raddr;
   int attrlen;
   int rtnetlink_sk;
   uid_t euid;

   if (sockscf.external.addrc <= 1
   ||  sockscf.external.rotation == ROTATION_NONE)
      return getdefaultexternal();

   /* get a NETLINK_ROUTE socket */
   socks_seteuid(&euid, sockscf.uid.privileged);
   rtnetlink_sk = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
   socks_reseteuid(sockscf.uid.privileged, euid);

   if (rtnetlink_sk == -1) {
      swarn("%s: socket(NETLINK_ROUTE)", function);
      return getdefaultexternal();
   }

   /*
    * Build the necessary data structures to get routing info.
    * The structures are:
    *   nlmsghdr - message header for netlink requests
    *      It specifies RTM_GETROUTE for get routing table info
    *   rtmsg - for routing table requests
    *   rtattr - Specifies RTA_DST indicating that the payload contains a
    *      destination address
    * the payload - the destination address
    */
   bzero(&req, sizeof(req));
   req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
   req.nh.nlmsg_flags = NLM_F_REQUEST;
   req.nh.nlmsg_type = RTM_GETROUTE;

   req.rt.rtm_family = AF_INET;
   req.rt.rtm_dst_len = 0;
   req.rt.rtm_src_len = 0;
   req.rt.rtm_tos = 0;
   req.rt.rtm_table = RT_TABLE_UNSPEC;
   req.rt.rtm_protocol = RTPROT_UNSPEC;
   req.rt.rtm_scope = RT_SCOPE_UNIVERSE;
   req.rt.rtm_type = RTN_UNICAST;
   req.rt.rtm_flags = 0;

   rta = (struct rtattr *)(((char *) &req) + NLMSG_ALIGN(req.nh.nlmsg_len));
   rta->rta_type = RTA_DST;
   rta->rta_len = RTA_LENGTH(sizeof(destaddr));

   req.nh.nlmsg_len = NLMSG_ALIGN(req.nh.nlmsg_len) + rta->rta_len;

   /* send the request and get the response. */
   memcpy(RTA_DATA(rta), &destaddr, sizeof(destaddr));
   if (send(rtnetlink_sk, &req, req.nh.nlmsg_len, 0)
   != (ssize_t)req.nh.nlmsg_len) {
      swarn("%s: send() to netlink failed", function);
      close(rtnetlink_sk);
      return getdefaultexternal();
   }

   if (recv(rtnetlink_sk, &buf, sizeof(buf), 0) == -1) {
      swarn("%s: recv() from netlink failed", function);
      close(rtnetlink_sk);
      return getdefaultexternal();
   }

   /*
    * Walk the response structures to find the one that contains
    * RTA_PREFSRC in order to get the local source address to bind to.
    */
   rhdr = (struct nlmsghdr *)buf;
   rrt = (struct rtmsg *)NLMSG_DATA(rhdr);
   attrlen = sizeof(buf) - sizeof(struct nlmsghdr) - sizeof(struct rtmsg);

   bzero(&raddr, sizeof(raddr));
   raddr.sa_family = AF_INET;

   for (rrta = (struct rtattr *)((char *)rrt + sizeof(struct rtmsg));
         RTA_OK(rrta, attrlen);
         rrta = (struct rtattr *)RTA_NEXT(rrta, attrlen)) {
      if (rrta->rta_type == RTA_PREFSRC) {
         TOIN(&raddr)->sin_addr = *(struct in_addr *)RTA_DATA(rrta);

         if (!isonexternal(&raddr)) {
            swarn("%s: address %s selected, but not set on external",
            function, sockaddr2string(&raddr, a, sizeof(a)));

            close(rtnetlink_sk);
            return getdefaultexternal();
         }

         slog(LOG_DEBUG, "%s: address %s selected for dst %s",
         function, sockaddr2string(&raddr, a, sizeof(a)), inet_ntoa(destaddr));

         close(rtnetlink_sk);
         return TOIN(&raddr)->sin_addr;
      }
   }

   slog(LOG_DEBUG, "%s: can't find a gateway for %s, using defaultexternal",
   function, inet_ntoa(destaddr));
   close(rtnetlink_sk);
   return getdefaultexternal();
}
#elif HAVE_ROUTEINFO_BSD /* !HAVE_ROUTEINFO_LINUX */

#define ROUNDUP(a) \
       ((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))
#define BUFLEN   (  sizeof(struct rt_msghdr) \
                  + sizeof(struct sockaddr_storage) * RTAX_MAX)

#define SEQ   9999

#ifndef RTAX_GATEWAY
#define RTAX_GATEWAY 1
#endif /* !RTAX_GATEWAY */

struct in_addr
getifa(destaddr)
   struct in_addr destaddr;
{
   const char *function = "getifa()";
   int              sockfd, i;
   char             buf[BUFLEN], a[MAXSOCKADDRSTRING], *cp;
   pid_t            pid;
   struct rt_msghdr *rtm;
   struct sockaddr  *sa, *ifa;
   uid_t euid;

   if (sockscf.external.addrc <= 1
   ||  sockscf.external.rotation == ROTATION_NONE)
      return getdefaultexternal();

   socks_seteuid(&euid, sockscf.uid.privileged);
   sockfd = socket(AF_ROUTE, SOCK_RAW, 0);   /* need superuser privileges */
   socks_reseteuid(sockscf.uid.privileged, euid);

   if (sockfd == -1) {
      swarn("%s: socket(AF_ROUTE)", function);
      return getdefaultexternal();
   }

   /*
    * Build the necessary data structures to get routing info.
    * The structures are:
    *   rt_msghdr - Specifies RTM_GET for getting routing table
    *      info
    *   sockaddr - contains the destination address
    */
   bzero(buf, sizeof(buf));
   rtm                     = (struct rt_msghdr *) buf;
   rtm->rtm_msglen         = sizeof(struct rt_msghdr) +
                             sizeof(struct sockaddr_in);
   rtm->rtm_version        = RTM_VERSION;
   rtm->rtm_type           = RTM_GET;
   rtm->rtm_addrs          = RTA_DST | RTA_IFA;
   rtm->rtm_pid            = pid = getpid();
   rtm->rtm_seq            = SEQ;
   rtm->rtm_flags          = RTF_UP | RTF_HOST | RTF_GATEWAY;

   sa = (struct sockaddr *) (rtm + 1);
   /* LINTED pointer casts may be troublesome */
   TOIN(sa)->sin_family    = AF_INET;
   /* LINTED pointer casts may be troublesome */
   TOIN(sa)->sin_addr      = destaddr;
   /* LINTED pointer casts may be troublesome */
   TOIN(sa)->sin_port      = htons(0);
#if HAVE_SOCKADDR_SA_LEN
   sa->sa_len = sizeof(struct sockaddr_in);
#endif /* HAVE_SOCKADDR_SA_LEN */

   /* send the request and get the response */
   if (write(sockfd, rtm, (size_t)rtm->rtm_msglen) != rtm->rtm_msglen) {
      swarn("%s: write() to AF_ROUTE failed", function);
      close(sockfd);
      return getdefaultexternal();
   }

   bzero(buf, sizeof(buf));
   do {
      if (read(sockfd, rtm, sizeof(buf)) == -1) {
         swarn("%s: read from AF_ROUTE failed", function);
         close(sockfd);

         return getdefaultexternal();
      }
   } while (rtm->rtm_type != RTM_GET
         || rtm->rtm_seq  != SEQ
         || rtm->rtm_pid  != pid);

   /*
    * iterate over the address structure extracting only the relevant
    * addresses.
    */
   cp  = (char *)(rtm + 1);
   sa = (struct sockaddr *)cp;

   ifa = NULL;
   for (i = 0; (i < RTAX_MAX) && (cp < buf + sizeof(buf)); i++) {
      switch (i) {
         case RTAX_GATEWAY:
            if (!(rtm->rtm_addrs & RTA_GATEWAY)) {
               swarnx("%s: can't find gateway for %s, using defaultexternal",
               function, inet_ntoa(destaddr));
               close(sockfd);

               return getdefaultexternal();
            }
            break;

         case RTAX_IFA:
            if (!(rtm->rtm_addrs & RTA_IFA)
            ||  TOIN(sa)->sin_family != AF_INET) {
              swarnx("%s: can't find ifa for %s, using defaultexternal",
              function, inet_ntoa(destaddr));
              close(sockfd);
              return getdefaultexternal();
            }
	    ifa = sa;
            break;
      }

      if (rtm->rtm_addrs & (1 << i)) {
	 if (sa->sa_family == AF_INET)
	    cp = cp + ROUNDUP(sizeof(struct sockaddr_in));
#ifdef AF_INET6
	 else if (sa->sa_family == AF_INET6)
	    cp = cp + ROUNDUP(sizeof(struct sockaddr_in6));
#endif /* AF_INET6 */
	 else if (sa->sa_family == AF_LINK)
	    cp = cp + ROUNDUP(sizeof(struct sockaddr_dl));
	 else
	    SERRX(sa->sa_family);
	 sa = (struct sockaddr *)cp;
      }
   }

   close(sockfd);

   SASSERTX(ifa != NULL);

   if (!isonexternal(ifa)) {
      swarnx("%s: address %s selected, but not set for external interface",
      function, sockaddr2string(ifa, a, sizeof(a)));

      return getdefaultexternal();
   }

   slog(LOG_DEBUG, "%s: address %s selected for dst %s",
   function, sockaddr2string(ifa, a, sizeof(a)), inet_ntoa(destaddr));

   /* LINTED pointer casts may be troublesome */
   return TOIN(ifa)->sin_addr;
}

#else /* HAVE_ROUTEINFO_BSD */
struct in_addr
getifa(destaddr)
   struct in_addr destaddr;
{
   return getdefaultexternal();
}
#endif /* !HAVE_ROUTEINFO_BSD */

static struct in_addr
getdefaultexternal(void)
{
   const char *function = "getdefaultexternal()";
   struct sockaddr bound;

   slog(LOG_DEBUG, function);

   /* find address to bind on clients behalf */
   switch ((*sockscf.external.addrv).atype) {
      case SOCKS_ADDR_IFNAME:
         if (ifname2sockaddr((*sockscf.external.addrv).addr.ifname, 0,
         &bound, NULL) == NULL) {
            swarnx("%s: can't find external interface/address: %s",
            function, (*sockscf.external.addrv).addr.ifname);

            /* LINTED pointer casts may be troublesome */
            TOIN(&bound)->sin_addr.s_addr = htonl(INADDR_NONE);
         }
         break;

      case SOCKS_ADDR_IPV4: {
         struct sockshost_t host;

         sockshost2sockaddr(ruleaddr2sockshost(&*sockscf.external.addrv,
         &host, SOCKS_TCP), &bound);
         break;
      }

      default:
         SERRX((*sockscf.external.addrv).atype);
   }

   /* LINTED pointer casts may be troublesome */
   return TOIN(&bound)->sin_addr;
}

static int
isonexternal(addr)
   const struct sockaddr *addr;
{
/*   const char *function = "isonexternal()"; */
   int i;

   for (i = 0; i < sockscf.external.addrc; ++i) {
      struct sockaddr check;
      int match = 0;

      switch (sockscf.external.addrv[i].atype) {
         case SOCKS_ADDR_IFNAME: {
            int ifi;

            ifi = 0;
            while (ifname2sockaddr(sockscf.external.addrv[i].addr.ifname,
            ifi++, &check, NULL) != NULL)
               /* LINTED pointer casts may be troublesome */
               if (TOIN(&check)->sin_addr.s_addr
               == TOCIN(addr)->sin_addr.s_addr) {
                  match = 1;
                  break;
               }
            }
            break;

         case SOCKS_ADDR_IPV4:
            /* LINTED pointer casts may be troublesome */
            if (sockscf.external.addrv[i].addr.ipv4.ip.s_addr
            == TOCIN(addr)->sin_addr.s_addr)
               match = 1;
            break;

         default:
            SERRX((*sockscf.external.addrv).atype);
      }

      if (match)
         break;
   }

   if (i == sockscf.external.addrc)
      return 0;

   return 1;
}