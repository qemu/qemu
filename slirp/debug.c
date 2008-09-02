/*
 * Copyright (c) 1995 Danny Gasparovski.
 * Portions copyright (c) 2000 Kelly Price.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#include <slirp.h>

FILE *dfd = NULL;
#ifdef DEBUG
int dostats = 1;
#else
int dostats = 0;
#endif
int slirp_debug = 0;

/* Carry over one item from main.c so that the tty's restored.
 * Only done when the tty being used is /dev/tty --RedWolf */
#ifndef CONFIG_QEMU
extern struct termios slirp_tty_settings;
extern int slirp_tty_restore;


void
debug_init(file, dbg)
	char *file;
	int dbg;
{
	/* Close the old debugging file */
	if (dfd)
	   fclose(dfd);

	dfd = fopen(file,"w");
	if (dfd != NULL) {
#if 0
		fprintf(dfd,"Slirp %s - Debugging Started.\n", SLIRP_VERSION);
#endif
		fprintf(dfd,"Debugging Started level %i.\r\n",dbg);
		fflush(dfd);
		slirp_debug = dbg;
	} else {
		lprint("Error: Debugging file \"%s\" could not be opened: %s\r\n",
			file, strerror(errno));
	}
}

/*
 * Dump a packet in the same format as tcpdump -x
 */
#ifdef DEBUG
void
dump_packet(dat, n)
	void *dat;
	int n;
{
	u_char *pptr = (u_char *)dat;
	int j,k;

	n /= 16;
	n++;
	DEBUG_MISC((dfd, "PACKET DUMPED: \n"));
	for(j = 0; j < n; j++) {
		for(k = 0; k < 6; k++)
			DEBUG_MISC((dfd, "%02x ", *pptr++));
		DEBUG_MISC((dfd, "\n"));
		fflush(dfd);
	}
}
#endif
#endif

#ifdef LOG_ENABLED
#if 0
/*
 * Statistic routines
 *
 * These will print statistics to the screen, the debug file (dfd), or
 * a buffer, depending on "type", so that the stats can be sent over
 * the link as well.
 */

static void
ttystats(ttyp)
	struct ttys *ttyp;
{
	struct slirp_ifstats *is = &ttyp->ifstats;
	char buff[512];

	lprint(" \r\n");

	if (IF_COMP & IF_COMPRESS)
	   strcpy(buff, "on");
	else if (IF_COMP & IF_NOCOMPRESS)
	   strcpy(buff, "off");
	else
	   strcpy(buff, "off (for now)");
	lprint("Unit %d:\r\n", ttyp->unit);
	lprint("  using %s encapsulation (VJ compression is %s)\r\n", (
#ifdef USE_PPP
	       ttyp->proto==PROTO_PPP?"PPP":
#endif
	       "SLIP"), buff);
	lprint("  %d baudrate\r\n", ttyp->baud);
	lprint("  interface is %s\r\n", ttyp->up?"up":"down");
	lprint("  using fd %d, guardian pid is %d\r\n", ttyp->fd, ttyp->pid);
#ifndef FULL_BOLT
	lprint("  towrite is %d bytes\r\n", ttyp->towrite);
#endif
	if (ttyp->zeros)
	   lprint("  %d zeros have been typed\r\n", ttyp->zeros);
	else if (ttyp->ones)
	   lprint("  %d ones have been typed\r\n", ttyp->ones);
	lprint("Interface stats:\r\n");
	lprint("  %6d output packets sent (%d bytes)\r\n", is->out_pkts, is->out_bytes);
	lprint("  %6d output packets dropped (%d bytes)\r\n", is->out_errpkts, is->out_errbytes);
	lprint("  %6d input packets received (%d bytes)\r\n", is->in_pkts, is->in_bytes);
	lprint("  %6d input packets dropped (%d bytes)\r\n", is->in_errpkts, is->in_errbytes);
	lprint("  %6d bad input packets\r\n", is->in_mbad);
}

static void
allttystats(void)
{
	struct ttys *ttyp;

	for (ttyp = ttys; ttyp; ttyp = ttyp->next)
	   ttystats(ttyp);
}
#endif

static void
ipstats(void)
{
	lprint(" \r\n");

	lprint("IP stats:\r\n");
	lprint("  %6d total packets received (%d were unaligned)\r\n",
			ipstat.ips_total, ipstat.ips_unaligned);
	lprint("  %6d with incorrect version\r\n", ipstat.ips_badvers);
	lprint("  %6d with bad header checksum\r\n", ipstat.ips_badsum);
	lprint("  %6d with length too short (len < sizeof(iphdr))\r\n", ipstat.ips_tooshort);
	lprint("  %6d with length too small (len < ip->len)\r\n", ipstat.ips_toosmall);
	lprint("  %6d with bad header length\r\n", ipstat.ips_badhlen);
	lprint("  %6d with bad packet length\r\n", ipstat.ips_badlen);
	lprint("  %6d fragments received\r\n", ipstat.ips_fragments);
	lprint("  %6d fragments dropped\r\n", ipstat.ips_fragdropped);
	lprint("  %6d fragments timed out\r\n", ipstat.ips_fragtimeout);
	lprint("  %6d packets reassembled ok\r\n", ipstat.ips_reassembled);
	lprint("  %6d outgoing packets fragmented\r\n", ipstat.ips_fragmented);
	lprint("  %6d total outgoing fragments\r\n", ipstat.ips_ofragments);
	lprint("  %6d with bad protocol field\r\n", ipstat.ips_noproto);
	lprint("  %6d total packets delivered\r\n", ipstat.ips_delivered);
}

#ifndef CONFIG_QEMU
static void
vjstats(void)
{
	lprint(" \r\n");

	lprint("VJ compression stats:\r\n");

	lprint("  %6d outbound packets (%d compressed)\r\n",
	       comp_s.sls_packets, comp_s.sls_compressed);
	lprint("  %6d searches for connection stats (%d misses)\r\n",
	       comp_s.sls_searches, comp_s.sls_misses);
	lprint("  %6d inbound uncompressed packets\r\n", comp_s.sls_uncompressedin);
	lprint("  %6d inbound compressed packets\r\n", comp_s.sls_compressedin);
	lprint("  %6d inbound unknown type packets\r\n", comp_s.sls_errorin);
	lprint("  %6d inbound packets tossed due to error\r\n", comp_s.sls_tossed);
}
#endif

static void
tcpstats(void)
{
	lprint(" \r\n");

	lprint("TCP stats:\r\n");

	lprint("  %6d packets sent\r\n", tcpstat.tcps_sndtotal);
	lprint("          %6d data packets (%d bytes)\r\n",
			tcpstat.tcps_sndpack, tcpstat.tcps_sndbyte);
	lprint("          %6d data packets retransmitted (%d bytes)\r\n",
			tcpstat.tcps_sndrexmitpack, tcpstat.tcps_sndrexmitbyte);
	lprint("          %6d ack-only packets (%d delayed)\r\n",
			tcpstat.tcps_sndacks, tcpstat.tcps_delack);
	lprint("          %6d URG only packets\r\n", tcpstat.tcps_sndurg);
	lprint("          %6d window probe packets\r\n", tcpstat.tcps_sndprobe);
	lprint("          %6d window update packets\r\n", tcpstat.tcps_sndwinup);
	lprint("          %6d control (SYN/FIN/RST) packets\r\n", tcpstat.tcps_sndctrl);
	lprint("          %6d times tcp_output did nothing\r\n", tcpstat.tcps_didnuttin);

	lprint("  %6d packets received\r\n", tcpstat.tcps_rcvtotal);
	lprint("          %6d acks (for %d bytes)\r\n",
			tcpstat.tcps_rcvackpack, tcpstat.tcps_rcvackbyte);
	lprint("          %6d duplicate acks\r\n", tcpstat.tcps_rcvdupack);
	lprint("          %6d acks for unsent data\r\n", tcpstat.tcps_rcvacktoomuch);
	lprint("          %6d packets received in sequence (%d bytes)\r\n",
			tcpstat.tcps_rcvpack, tcpstat.tcps_rcvbyte);
        lprint("          %6d completely duplicate packets (%d bytes)\r\n",
			tcpstat.tcps_rcvduppack, tcpstat.tcps_rcvdupbyte);

	lprint("          %6d packets with some duplicate data (%d bytes duped)\r\n",
			tcpstat.tcps_rcvpartduppack, tcpstat.tcps_rcvpartdupbyte);
	lprint("          %6d out-of-order packets (%d bytes)\r\n",
			tcpstat.tcps_rcvoopack, tcpstat.tcps_rcvoobyte);
	lprint("          %6d packets of data after window (%d bytes)\r\n",
			tcpstat.tcps_rcvpackafterwin, tcpstat.tcps_rcvbyteafterwin);
	lprint("          %6d window probes\r\n", tcpstat.tcps_rcvwinprobe);
	lprint("          %6d window update packets\r\n", tcpstat.tcps_rcvwinupd);
	lprint("          %6d packets received after close\r\n", tcpstat.tcps_rcvafterclose);
	lprint("          %6d discarded for bad checksums\r\n", tcpstat.tcps_rcvbadsum);
	lprint("          %6d discarded for bad header offset fields\r\n",
			tcpstat.tcps_rcvbadoff);

	lprint("  %6d connection requests\r\n", tcpstat.tcps_connattempt);
	lprint("  %6d connection accepts\r\n", tcpstat.tcps_accepts);
	lprint("  %6d connections established (including accepts)\r\n", tcpstat.tcps_connects);
	lprint("  %6d connections closed (including %d drop)\r\n",
			tcpstat.tcps_closed, tcpstat.tcps_drops);
	lprint("  %6d embryonic connections dropped\r\n", tcpstat.tcps_conndrops);
	lprint("  %6d segments we tried to get rtt (%d succeeded)\r\n",
			tcpstat.tcps_segstimed, tcpstat.tcps_rttupdated);
	lprint("  %6d retransmit timeouts\r\n", tcpstat.tcps_rexmttimeo);
	lprint("          %6d connections dropped by rxmt timeout\r\n",
			tcpstat.tcps_timeoutdrop);
	lprint("  %6d persist timeouts\r\n", tcpstat.tcps_persisttimeo);
	lprint("  %6d keepalive timeouts\r\n", tcpstat.tcps_keeptimeo);
	lprint("          %6d keepalive probes sent\r\n", tcpstat.tcps_keepprobe);
	lprint("          %6d connections dropped by keepalive\r\n", tcpstat.tcps_keepdrops);
	lprint("  %6d correct ACK header predictions\r\n", tcpstat.tcps_predack);
	lprint("  %6d correct data packet header predictions\n", tcpstat.tcps_preddat);
	lprint("  %6d TCP cache misses\r\n", tcpstat.tcps_socachemiss);


/*	lprint("    Packets received too short:		%d\r\n", tcpstat.tcps_rcvshort); */
/*	lprint("    Segments dropped due to PAWS:	%d\r\n", tcpstat.tcps_pawsdrop); */

}

static void
udpstats(void)
{
        lprint(" \r\n");

	lprint("UDP stats:\r\n");
	lprint("  %6d datagrams received\r\n", udpstat.udps_ipackets);
	lprint("  %6d with packets shorter than header\r\n", udpstat.udps_hdrops);
	lprint("  %6d with bad checksums\r\n", udpstat.udps_badsum);
	lprint("  %6d with data length larger than packet\r\n", udpstat.udps_badlen);
	lprint("  %6d UDP socket cache misses\r\n", udpstat.udpps_pcbcachemiss);
	lprint("  %6d datagrams sent\r\n", udpstat.udps_opackets);
}

static void
icmpstats(void)
{
	lprint(" \r\n");
	lprint("ICMP stats:\r\n");
	lprint("  %6d ICMP packets received\r\n", icmpstat.icps_received);
	lprint("  %6d were too short\r\n", icmpstat.icps_tooshort);
	lprint("  %6d with bad checksums\r\n", icmpstat.icps_checksum);
	lprint("  %6d with type not supported\r\n", icmpstat.icps_notsupp);
	lprint("  %6d with bad type feilds\r\n", icmpstat.icps_badtype);
	lprint("  %6d ICMP packets sent in reply\r\n", icmpstat.icps_reflect);
}

static void
mbufstats(void)
{
	struct mbuf *m;
	int i;

        lprint(" \r\n");

	lprint("Mbuf stats:\r\n");

	lprint("  %6d mbufs allocated (%d max)\r\n", mbuf_alloced, mbuf_max);

	i = 0;
	for (m = m_freelist.m_next; m != &m_freelist; m = m->m_next)
		i++;
	lprint("  %6d mbufs on free list\r\n",  i);

	i = 0;
	for (m = m_usedlist.m_next; m != &m_usedlist; m = m->m_next)
		i++;
	lprint("  %6d mbufs on used list\r\n",  i);
        lprint("  %6d mbufs queued as packets\r\n\r\n", if_queued);
}

static void
sockstats(void)
{
	char buff[256];
	int n;
	struct socket *so;

        lprint(" \r\n");

	lprint(
	   "Proto[state]     Sock     Local Address, Port  Remote Address, Port RecvQ SendQ\r\n");

	for (so = tcb.so_next; so != &tcb; so = so->so_next) {

		n = sprintf(buff, "tcp[%s]", so->so_tcpcb?tcpstates[so->so_tcpcb->t_state]:"NONE");
		while (n < 17)
		   buff[n++] = ' ';
		buff[17] = 0;
		lprint("%s %3d   %15s %5d ",
				buff, so->s,
				inet_ntoa(so->so_laddr), ntohs(so->so_lport));
		lprint("%15s %5d %5d %5d\r\n",
				inet_ntoa(so->so_faddr), ntohs(so->so_fport),
				so->so_rcv.sb_cc, so->so_snd.sb_cc);
	}

	for (so = udb.so_next; so != &udb; so = so->so_next) {

		n = sprintf(buff, "udp[%d sec]", (so->so_expire - curtime) / 1000);
		while (n < 17)
		   buff[n++] = ' ';
		buff[17] = 0;
		lprint("%s %3d  %15s %5d  ",
				buff, so->s,
				inet_ntoa(so->so_laddr), ntohs(so->so_lport));
		lprint("%15s %5d %5d %5d\r\n",
				inet_ntoa(so->so_faddr), ntohs(so->so_fport),
				so->so_rcv.sb_cc, so->so_snd.sb_cc);
	}
}
#endif

#ifndef CONFIG_QEMU
void
slirp_exit(exit_status)
	int exit_status;
{
	struct ttys *ttyp;

	DEBUG_CALL("slirp_exit");
	DEBUG_ARG("exit_status = %d", exit_status);

	if (dostats) {
		lprint_print = (int (*) _P((void *, const char *, va_list)))vfprintf;
		if (!dfd)
		   debug_init("slirp_stats", 0xf);
		lprint_arg = (char **)&dfd;

		ipstats();
		tcpstats();
		udpstats();
		icmpstats();
		mbufstats();
		sockstats();
		allttystats();
		vjstats();
	}

	for (ttyp = ttys; ttyp; ttyp = ttyp->next)
	   tty_detached(ttyp, 1);

	if (slirp_forked) {
		/* Menendez time */
		if (kill(getppid(), SIGQUIT) < 0)
			lprint("Couldn't kill parent process %ld!\n",
			    (long) getppid());
    	}

	/* Restore the terminal if we gotta */
	if(slirp_tty_restore)
	  tcsetattr(0,TCSANOW, &slirp_tty_settings);  /* NOW DAMMIT! */
	exit(exit_status);
}
#endif

void
slirp_stats(void)
{
#ifdef LOG_ENABLED
    ipstats();
    tcpstats();
    udpstats();
    icmpstats();
    mbufstats();
    sockstats();
#else
    lprint("SLIRP statistics code not compiled.\n");
#endif
}
