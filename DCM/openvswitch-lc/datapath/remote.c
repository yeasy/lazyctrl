/*
 * Copyright (c) 2007-2011 IBM CRL.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#include <linux/if_vlan.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <net/checksum.h>

#include "datapath.h"
#include "remote.h"

#ifndef ETH_IP_HLEN
#define ETH_IP_HLEN 2
#endif
#ifndef IP_HLEN
#define IP_HLEN 20
#endif

/*crl*/
static const char gw_mac[]={0x0a,0x00,0x27,0x00,0x00,0x01};//mac of the core routable network's gw: 192.168.57.1
/*thu*/
//static const char gw_mac[]={0x08,0x00,0x27,0x00,0xbc,0x89};//mac of the core routable network gw:192.168.57.1

/**
 * add new mac header and ip header.
 */
int __remote_encapulation(struct datapath *dp, struct sk_buff *skb, int dst_ip)
{
    struct ethhdr *eth;
    struct iphdr *iph;

    if (skb_cow_head(skb, ETH_HLEN+ETH_IP_HLEN+IP_HLEN) < 0) { //make enough room at the head, new ether and ip header
        kfree_skb(skb);
        return -1;
    }

    /*add new eth_ip header*/
    skb_push(skb,ETH_IP_HLEN);
    *((unsigned short*)(skb->data)) = 0x3;

    /*add new ip header*/
    iph = (struct iphdr *)skb_push(skb,IP_HLEN);
    iph->ihl=5;
    iph->version=4;
    iph->tos=0;
    iph->tot_len = htons(skb->len);
    iph->id = htonl(0);
    iph->frag_off = 0;
    iph->ttl = 255;
    iph->protocol = LC_REMOTE_IP_PROTO;
    iph->saddr=htonl(dp->local_ip);
    iph->daddr=htonl(dst_ip);
    iph->check=0; //MUST be set to 0 first.
    iph->check=ip_fast_csum((unsigned char *)iph,iph->ihl); //csum here

    /*add new ethernet header*/
    eth = (struct ethhdr *)skb_push(skb,ETH_HLEN);
    memcpy(skb->data, skb->data+ETH_IP_HLEN+ETH_HLEN+IP_HLEN, ETH_HLEN);
    eth->h_proto = htons(ETH_P_IP); //ip packet
    memcpy(&(eth->h_dest),gw_mac,ETH_ALEN);

    skb->mac_header -= (ETH_IP_HLEN+IP_HLEN+ETH_HLEN);

    return 0;
}

/**
 * decapulate the mac header and ip header.
 */
int __remote_decapulation(struct sk_buff *skb)
{
	skb_reset_mac_header(skb);
    struct ethhdr *eth = eth_hdr(skb);
    skb_pull(skb,skb->mac_len);
    if (eth->h_proto != htons(ETH_P_IP)) {
#ifdef DEBUG
        pr_info("__remote_decapulation() no IP existed? proto=0x%x\n", ntohs(eth->h_proto));
#endif
        return -1;
    } else {
#ifdef DEBUG
        pr_info("__remote_decapulation() FOUND IP\n");
#endif
    }
	skb_reset_network_header(skb);
    struct iphdr *iph = ip_hdr(skb);
    skb_pull(skb,IP_HLEN);
    if( iph->protocol != LC_REMOTE_IP_PROTO) {
#ifdef DEBUG
        pr_info("__remote_decapulation() not ETHERIP header? proto=0x%x\n",iph->protocol);
#endif
        return -1;
    }
    skb_pull(skb, ETH_IP_HLEN);
    memcpy(skb->data, eth, 2*ETH_ALEN); //change to right mac header.
    return 0;
}
