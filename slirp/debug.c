/*
 * Copyright (c) 1995 Danny Gasparovski.
 * Portions copyright (c) 2000 Kelly Price.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#include <slirp.h>

FILE *dfd = NULL;
int slirp_debug = 0;

#ifdef LOG_ENABLED
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
#else
    lprint("SLIRP statistics code not compiled.\n");
#endif
}
