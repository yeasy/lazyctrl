/*
 * Copyright (c) 2007-2012 Nicira, Inc.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/if_arp.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/jhash.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/etherdevice.h>
#include <linux/genetlink.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/version.h>
#include <linux/ethtool.h>
#include <linux/wait.h>
#include <asm/div64.h>
#include <linux/highmem.h>
#include <linux/netfilter_bridge.h>
#include <linux/netfilter_ipv4.h>
#include <linux/inetdevice.h>
#include <linux/list.h>
#include <linux/openvswitch.h>
#include <linux/rculist.h>
#include <linux/dmi.h>
#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <net/genetlink.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>

#include "checksum.h"
#include "bf-gdt.h"
#include "datapath.h"
#include "flow.h"
#include "genl_exec.h"
#include "vlan.h"
#include "tunnel.h"
#include "vport-internal_dev.h"

#ifdef LC_ENABLE
#ifndef LC_LOCAL_EDGE_IP
#define LC_LOCAL_EDGE_IP LC_BF_LOCAL_ID //local edge sw's ip
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18) || \
    LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
#error Kernels before 2.6.18 or after 3.5 are not supported by this version of Open vSwitch.
#endif

#define REHASH_FLOW_INTERVAL (10 * 60 * HZ)
static void rehash_flow_table(struct work_struct *work);
static DECLARE_DELAYED_WORK(rehash_flow_wq, rehash_flow_table);

int ovs_net_id __read_mostly;

int (*ovs_dp_ioctl_hook)(struct net_device *dev, struct ifreq *rq, int cmd);
EXPORT_SYMBOL(ovs_dp_ioctl_hook);

/**
 * DOC: Locking:
 *
 * Writes to device state (add/remove datapath, port, set operations on vports,
 * etc.) are protected by RTNL.
 *
 * Writes to other state (flow table modifications, set miscellaneous datapath
 * parameters, etc.) are protected by genl_mutex.  The RTNL lock nests inside
 * genl_mutex.
 *
 * Reads are protected by RCU.
 *
 * There are a few special cases (mostly stats) that have their own
 * synchronization but they nest under all of above and don't interact with
 * each other.
 */

static struct vport *new_vport(const struct vport_parms *);
static int queue_gso_packets(struct net *, int dp_ifindex, struct sk_buff *,
			     const struct dp_upcall_info *);
static int queue_userspace_packet(struct net *, int dp_ifindex,
				  struct sk_buff *,
				  const struct dp_upcall_info *);

/* Must be called with rcu_read_lock, genl_mutex, or RTNL lock. */
static struct datapath *get_dp(struct net *net, int dp_ifindex)
{
	struct datapath *dp = NULL;
	struct net_device *dev;

	rcu_read_lock();
	dev = dev_get_by_index_rcu(net, dp_ifindex);
	if (dev) {
		struct vport *vport = ovs_internal_dev_get_vport(dev);
		if (vport)
			dp = vport->dp;
	}
	rcu_read_unlock();

	return dp;
}

/* Must be called with rcu_read_lock or RTNL lock. */
const char *ovs_dp_name(const struct datapath *dp)
{
	struct vport *vport = ovs_vport_rtnl_rcu(dp, OVSP_LOCAL);
	return vport->ops->get_name(vport);
}

static int get_dpifindex(struct datapath *dp)
{
	struct vport *local;
	int ifindex;

	rcu_read_lock();

	local = ovs_vport_rcu(dp, OVSP_LOCAL);
	if (local)
		ifindex = local->ops->get_ifindex(local);
	else
		ifindex = 0;

	rcu_read_unlock();

	return ifindex;
}

static size_t br_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct ifinfomsg))
	       + nla_total_size(IFNAMSIZ) /* IFLA_IFNAME */
	       + nla_total_size(MAX_ADDR_LEN) /* IFLA_ADDRESS */
	       + nla_total_size(4) /* IFLA_MASTER */
	       + nla_total_size(4) /* IFLA_MTU */
	       + nla_total_size(1); /* IFLA_OPERSTATE */
}

/* Caller must hold RTNL lock. */
static int dp_fill_ifinfo(struct sk_buff *skb,
			  const struct vport *port,
			  int event, unsigned int flags)
{
	struct datapath *dp = port->dp;
	struct ifinfomsg *hdr;
	struct nlmsghdr *nlh;

	if (!port->ops->get_ifindex)
		return -ENODEV;

	nlh = nlmsg_put(skb, 0, 0, event, sizeof(*hdr), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	hdr = nlmsg_data(nlh);
	hdr->ifi_family = AF_BRIDGE;
	hdr->__ifi_pad = 0;
	hdr->ifi_type = ARPHRD_ETHER;
	hdr->ifi_index = port->ops->get_ifindex(port);
	hdr->ifi_flags = port->ops->get_dev_flags(port);
	hdr->ifi_change = 0;

	if (nla_put_string(skb, IFLA_IFNAME, port->ops->get_name(port)) ||
	    nla_put_u32(skb, IFLA_MASTER, get_dpifindex(dp)) ||
	    nla_put_u32(skb, IFLA_MTU, port->ops->get_mtu(port)) ||
#ifdef IFLA_OPERSTATE
	    nla_put_u8(skb, IFLA_OPERSTATE,
		       port->ops->is_running(port) ?
				port->ops->get_operstate(port) :
				IF_OPER_DOWN) ||
#endif
	    nla_put(skb, IFLA_ADDRESS, ETH_ALEN, port->ops->get_addr(port)))
		goto nla_put_failure;

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

/* Caller must hold RTNL lock. */
static void dp_ifinfo_notify(int event, struct vport *port)
{
	struct sk_buff *skb;
	int err;

	skb = nlmsg_new(br_nlmsg_size(), GFP_KERNEL);
	if (!skb) {
		err = -ENOBUFS;
		goto err;
	}

	err = dp_fill_ifinfo(skb, port, event, 0);
	if (err < 0) {
		if (err == -ENODEV) {
			goto out;
		} else {
			/* -EMSGSIZE implies BUG in br_nlmsg_size() */
			WARN_ON(err == -EMSGSIZE);
			goto err;
		}
	}

	rtnl_notify(skb, ovs_dp_get_net(port->dp), 0, RTNLGRP_LINK, NULL, GFP_KERNEL);

	return;
err:
	rtnl_set_sk_err(ovs_dp_get_net(port->dp), RTNLGRP_LINK, err);
out:
	kfree_skb(skb);
}

static void release_dp(struct kobject *kobj)
{
	struct datapath *dp = container_of(kobj, struct datapath, ifobj);
	kfree(dp);
}

static struct kobj_type dp_ktype = {
	.release = release_dp
};

static void destroy_dp_rcu(struct rcu_head *rcu)
{
	struct datapath *dp = container_of(rcu, struct datapath, rcu);

	ovs_flow_tbl_destroy((__force struct flow_table *)dp->table);
	free_percpu(dp->stats_percpu);
	release_net(ovs_dp_get_net(dp));
	kfree(dp->ports);
	kobject_put(&dp->ifobj);
}

static struct hlist_head *vport_hash_bucket(const struct datapath *dp,
					    u16 port_no)
{
	return &dp->ports[port_no & (DP_VPORT_HASH_BUCKETS - 1)];
}

struct vport *ovs_lookup_vport(const struct datapath *dp, u16 port_no)
{
	struct vport *vport;
	struct hlist_node *n;
	struct hlist_head *head;

	head = vport_hash_bucket(dp, port_no);
	hlist_for_each_entry_rcu(vport, n, head, dp_hash_node) {
		if (vport->port_no == port_no)
			return vport;
	}
	return NULL;
}

/* Called with RTNL lock and genl_lock. */
static struct vport *new_vport(const struct vport_parms *parms)
{
	struct vport *vport;

	vport = ovs_vport_add(parms);
	if (!IS_ERR(vport)) {
		struct datapath *dp = parms->dp;
		struct hlist_head *head = vport_hash_bucket(dp, vport->port_no);

		hlist_add_head_rcu(&vport->dp_hash_node, head);
		dp_ifinfo_notify(RTM_NEWLINK, vport);
	}
	return vport;
}

/* Called with RTNL lock. */
void ovs_dp_detach_port(struct vport *p)
{
	ASSERT_RTNL();

	if (p->port_no != OVSP_LOCAL)
		ovs_dp_sysfs_del_if(p);

	dp_ifinfo_notify(RTM_DELLINK, p);

	/* First drop references to device. */
	hlist_del_rcu(&p->dp_hash_node);

	/* Then destroy it. */
	ovs_vport_del(p);
}

static inline void pr_mac(const char *info, unsigned char *src, unsigned char *dst, unsigned short type)
{
    pr_info("%s: L2=(%x:%x:%x:%x:%x:%x -> %x:%x:%x:%x:%x:%x, type=0x%x)\n", info,
            src[0],src[1],src[2],src[3], src[4],src[5],
            dst[0],dst[1],dst[2],dst[3], dst[4],dst[5], type);
}

/* Must be called with rcu_read_lock. */
void ovs_dp_process_received_packet(struct vport *p, struct sk_buff *skb)
{
	struct datapath *dp = p->dp;
	struct sw_flow *flow;
	struct dp_stats_percpu *stats;
	u64 *stats_counter;
	int error = 0;
#ifdef LC_ENABLE
    struct bloom_filter *bf = NULL;
    unsigned char dst_mac[13] = {0};//mac: "0a00000027"
#endif

	stats = per_cpu_ptr(dp->stats_percpu, smp_processor_id());

#ifdef LC_ENABLE
    if (OVS_CB(skb)->encaped) { //remote pkt, do decapulation first
#ifdef DEBUG
        pr_info("DP process_received_packet(): Received REMOTE pkt, decap first.\n");
#endif
        error = ovs_execute_decapulation(skb);
        if(unlikely(error < 0)) {
            pr_warning("DP process_received_packet(): DECAP FAILURE.\n");
            kfree_skb(skb);
            return;
        }
    }
#endif

    if (!OVS_CB(skb)->flow) { //no flow associated with the pkt, normal case
        struct sw_flow_key key;
        int key_len;

        /* Extract flow from 'skb' into 'key'. */
        error = ovs_flow_extract(skb, p->port_no, &key, &key_len);
        if (unlikely(error)) {
            kfree_skb(skb);
            return;
        }
        
#ifdef DEBUG
        if (ntohs(key.eth.type) != 0x0806 && ntohs(key.eth.type) != 0x0800) { 
            pr_info("Drop unknown l2_type = 0x%x",ntohs(key.eth.type));
            kfree_skb(skb);
            return;
        }
#endif

#ifdef DEBUG
        if (!OVS_CB(skb)->encaped) {
            pr_info("DP process_received_packet(): Received LOCAL pkt.\n");
        }
        pr_mac("pkt mac header:",(unsigned char*)key.eth.src, (unsigned char*)key.eth.dst,
                ntohs(key.eth.type));
#endif
        /* Look up in local table. */
        flow = ovs_flow_tbl_lookup(rcu_dereference(dp->table), &key, key_len);

        if (unlikely(!flow)) { /*not found in local fwd table, usually the 1st pkt.*/
#ifdef LC_ENABLE
#ifdef DEBUG
            pr_info("NO found flow in local tbl");
#endif
            /*ip pkt from local host*/
            if (!OVS_CB(skb)->encaped && key.eth.type!=htons(0x0806)) {//local non-arp pkt
                sprintf(dst_mac,"%02x%02x%02x%02x%02x%02x",
                        key.eth.dst[0],key.eth.dst[1],key.eth.dst[2],key.eth.dst[3],key.eth.dst[4],key.eth.dst[5]);
                bf = bf_gdt_check(dp->gdt,dst_mac); //host in bf_gdt?
            }
            /*local_to_remote pkt, and in local bf-gdt. */
            if (!OVS_CB(skb)->encaped && likely(bf)) {
#ifdef DEBUG
                pr_info("Found %s in bf_gdt,bf_id=0x%x,will send REMOTE cmd:port=%u,ip=0x%x",dst_mac, bf->bf_id, bf->port_no, bf->bf_id);
#endif

                flow = kmalloc(sizeof(struct sw_flow), GFP_ATOMIC);
                if (IS_ERR(flow)) {
                    goto out;
                }
                spin_lock_init(&flow->lock);
                atomic_set(&flow->refcnt, 1);
                flow->sf_acts = NULL;
                flow->dead = false;
                u64 payload = (((u64)bf->port_no)<<32)+bf->bf_id;
                flow->sf_acts = kmalloc(sizeof(struct sw_flow_actions)+NLA_HDRLEN+NLA_ALIGN(sizeof(payload)), GFP_ATOMIC);
                flow->sf_acts->actions_len = NLA_HDRLEN + NLA_ALIGN(sizeof(payload));
                struct nlattr *action = flow->sf_acts->actions;
                action->nla_len = NLA_HDRLEN + sizeof(payload);
                action->nla_type = OVS_ACTION_ATTR_REMOTE;
                memcpy(nla_data(action), &payload, sizeof(payload));
            } else { /* Not in local table. Not in bf-gdt yet, then send to ovsd*/
#endif
#ifdef DEBUG
                pr_info("NO found %s in tbl/gdt, will send upcall", dst_mac);
#endif
                struct dp_upcall_info upcall;
                upcall.cmd = OVS_PACKET_CMD_MISS;
                upcall.key = &key;
                upcall.userdata = NULL;
                upcall.pid = p->upcall_pid;
                ovs_dp_upcall(dp, skb, &upcall);
                consume_skb(skb);
                stats_counter = &stats->n_missed;
                goto out;
#ifdef LC_ENABLE
            }
#endif
        }//if(!flow)
#ifdef DEBUG
        else 
        pr_info("Found flow in local tbl");
#endif
        OVS_CB(skb)->flow = flow;
    } /*now each pkt has an associated flow. */

    stats_counter = &stats->n_hit;
    ovs_flow_used(OVS_CB(skb)->flow, skb);

    /*run corresponding actions.*/
    ovs_execute_actions(dp, skb);

out:
    /* Update datapath statistics. */
    u64_stats_update_begin(&stats->sync);
    (*stats_counter)++;
    u64_stats_update_end(&stats->sync);

#ifdef LC_ENABLE
    if (likely(bf)){ //match bf-gdt
        if(flow) {
            if (atomic_dec_and_test(&flow->refcnt)) {
                if (flow->sf_acts)
                    kfree(flow->sf_acts);
                kfree(flow);
            }
        }
    }
#endif
}

static struct genl_family dp_packet_genl_family = {
    .id = GENL_ID_GENERATE,
    .hdrsize = sizeof(struct ovs_header),
    .name = OVS_PACKET_FAMILY,
    .version = OVS_PACKET_VERSION,
    .maxattr = OVS_PACKET_ATTR_MAX,
    SET_NETNSOK
};

#ifdef LC_ENABLE

static const struct nla_policy bf_gdt_policy[OVS_BF_GDT_ATTR_MAX + 1] = {
    [OVS_BF_GDT_ATTR_BF] = { .type = NLA_NESTED },
};

/**
 * family for the bf-gdt management, update the gdt
 */
static struct genl_family dp_bf_gdt_genl_family = {
    .id = GENL_ID_GENERATE,
    .hdrsize = sizeof(struct ovs_header),
    .name = OVS_BF_GDT_FAMILY,
    .version = OVS_BF_GDT_VERSION,
    .maxattr = OVS_BF_GDT_ATTR_MAX,
    SET_NETNSOK
};

/* Called with genl_lock. */
static int ovs_bf_gdt_cmd_fill_info(struct bloom_filter *bf, struct datapath *dp,
				  struct sk_buff *skb, u32 pid,
				  u32 seq, u32 flags, u8 cmd)
{
	const int skb_orig_len = skb->len;
	struct ovs_header *ovs_header;
	int err;

	ovs_header = genlmsg_put(skb, pid, seq, &dp_bf_gdt_genl_family, flags, cmd);
	if (!ovs_header)
		return -EMSGSIZE;

	ovs_header->dp_ifindex = get_dpifindex(dp);

    err = nla_put_u32(skb, OVS_BF_GDT_ATTR_KEY, bf->bf_id);
	if (err)
		goto nla_put_failure;
    err = nla_put(skb, OVS_BF_GDT_ATTR_BF,sizeof(struct bloom_filter),bf);

    if (err < 0 && skb_orig_len)
        goto error;

    return genlmsg_end(skb, ovs_header);

nla_put_failure:
    err = -EMSGSIZE;
error:
	genlmsg_cancel(skb, ovs_header);
	return err;
}

static struct sk_buff *ovs_bf_gdt_cmd_alloc_info(struct bloom_filter *bf)
{
	int len;

	/* bloom_filter */
	len = nla_total_size(8+4+sizeof(struct bloom_filter));
	len += NLMSG_ALIGN(sizeof(struct ovs_header));

	return genlmsg_new(len, GFP_KERNEL);
}

static struct sk_buff *ovs_bf_gdt_cmd_build_info(struct bloom_filter *bf,
					       struct datapath *dp,
					       u32 pid, u32 seq, u8 cmd)
{
	struct sk_buff *skb;
	int retval;

	skb = ovs_bf_gdt_cmd_alloc_info(bf);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	retval = ovs_bf_gdt_cmd_fill_info(bf, dp, skb, pid, seq, 0, cmd);
	BUG_ON(retval < 0);
	return skb;
}


/**
 * Update the local bf-gdt according to the received nlmsg from ovsd.
 */
static int ovs_bf_gdt_cmd_new_or_set(struct sk_buff *skb, struct genl_info *info)
{
    struct nlattr **a = info->attrs;
    struct ovs_header *ovs_header = info->userhdr;
    struct bloom_filter *bf;
    struct datapath *dp;
    int ret;
    struct bf_gdt *gdt;

    /* Extract bf. */
    if (!a[OVS_BF_GDT_ATTR_BF])
        goto error;
    if(nla_len(a[OVS_BF_GDT_ATTR_BF])!=sizeof(struct bloom_filter)){
        goto error;
    }

    bf = nla_data(a[OVS_BF_GDT_ATTR_BF]);
    dp = get_dp(sock_net(skb->sk), ovs_header->dp_ifindex);
    if (!dp)
        goto error;
#ifdef DEBUG
    pr_info(">>>ovs_bf_gdt_cmd_new_or_set(): bf_id=0x%x,len=%u.\n",bf->bf_id,bf->len);
#endif
    gdt = genl_dereference(dp->gdt);
    ret = bf_gdt_update_filter(gdt, bf);
    /*
    int i = 0;
    char tmp[256]={0};
    for (i=0;i<128;i++){
        sprintf(tmp+i%16,"%x",bf->array[i]);
        if(i%16==0){
            pr_info("%u",i/16);
        }
        if((i+1)%16==0){
            pr_info("%s",tmp);
        }
    }
    */

#ifdef DEBUG
    pr_info("<<<ovs_bf_gdt_cmd_new_or_set(),ret=%u",ret);
#endif
    return 0;
error:
    return -1;
}

/**
 * get bf_gdt according to the bf_id.
 */
static int ovs_bf_gdt_cmd_get(struct sk_buff *skb, struct genl_info *info)
{
    struct nlattr **a = info->attrs;
    struct ovs_header *ovs_header = info->userhdr;
    unsigned int bf_id;
    struct sk_buff *reply;
    struct datapath *dp;
    struct bf_gdt *gdt;
    struct bloom_filter *bf;

    if (!a[OVS_BF_GDT_ATTR_KEY])
        return -EINVAL;
    bf_id = nla_get_u32(a[OVS_BF_GDT_ATTR_KEY]);

    dp = get_dp(sock_net(skb->sk), ovs_header->dp_ifindex);
    if (!dp)
        return -ENODEV;

    gdt = genl_dereference(dp->gdt);
    if(!(bf=bf_gdt_find_filter(gdt,bf_id)))
        return -ENOENT;

    reply = ovs_bf_gdt_cmd_build_info(bf, dp, info->snd_pid, info->snd_seq, OVS_BF_GDT_CMD_NEW);
    if (IS_ERR(reply))
        return PTR_ERR(reply);

    return genlmsg_reply(reply, info);
}

/**
 * flush the bf-gdt table
 */
static int flush_bf_gdt(struct datapath *dp)
{
	struct bf_gdt *old_table;

	old_table = genl_dereference(dp->gdt);
   	bf_gdt_destroy(old_table);
    dp->gdt = NULL;
	return 0;
}

/**
 * del bf_gdt according to the bf_id.
 */
static int ovs_bf_gdt_cmd_del(struct sk_buff *skb, struct genl_info *info)
{
#ifdef DEBUG
    pr_info(">>>ovs_bf_gdt_cmd_del()");
#endif
	struct nlattr **a = info->attrs;
	struct ovs_header *ovs_header = info->userhdr;
	struct sk_buff *reply;
	struct datapath *dp;
	int err;
    unsigned int bf_id=0;
    struct bloom_filter *bf=NULL;
	struct bf_gdt *gdt;

	dp = get_dp(sock_net(skb->sk), ovs_header->dp_ifindex);
	if (!dp)
		return -ENODEV;

	if (!a[OVS_BF_GDT_ATTR_KEY])
		return flush_bf_gdt(dp);

    bf_id = nla_get_u32(a[OVS_BF_GDT_ATTR_KEY]);
	gdt = genl_dereference(dp->gdt);
    bf = bf_gdt_find_filter(gdt,bf_id);
	if (!bf)
		return -ENOENT;

	reply = ovs_bf_gdt_cmd_alloc_info(bf);
	if (!reply)
		return -ENOMEM;
	err = ovs_bf_gdt_cmd_fill_info(bf, dp, reply, info->snd_pid,
				     info->snd_seq, 0, OVS_BF_GDT_CMD_DEL);
	BUG_ON(err < 0);
    bf_gdt_del_filter(gdt,bf_id);

#ifdef DEBUG
    pr_info("<<<ovs_bf_gdt_cmd_del()");
#endif
    return 0;
}

/**
 * dump bf_gdt.
 */
static int ovs_bf_gdt_cmd_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct ovs_header *ovs_header = genlmsg_data(nlmsg_data(cb->nlh));
	struct datapath *dp;
	struct bf_gdt *gdt;
    struct bloom_filter *bf;
    int i = 0;

	dp = get_dp(sock_net(skb->sk), ovs_header->dp_ifindex);
	if (!dp)
		return -ENODEV;

	gdt = genl_dereference(dp->gdt);

    u32 bucket = cb->args[0]; //max num
    u32 last = cb->args[1]; //last
	for (i=0;i<bucket && i<gdt->nbf;i++) {
		bf = gdt->bf_array[i];
		if (!bf)
			break;
		if (ovs_bf_gdt_cmd_fill_info(bf, dp, skb,
					   NETLINK_CB(cb->skb).pid,
					   cb->nlh->nlmsg_seq, NLM_F_MULTI,
					   OVS_BF_GDT_CMD_NEW) < 0)
			break;
	}
	return skb->len;
}

/**
 * operation for the bf-gdt management, update the gdt
 */
static struct genl_ops dp_bf_gdt_genl_ops[] = {
	{ .cmd = OVS_BF_GDT_CMD_NEW,
	  .flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN privilege. */
	  .policy = bf_gdt_policy,
	  .doit = ovs_bf_gdt_cmd_new_or_set
	},
    { .cmd = OVS_BF_GDT_CMD_DEL,
	  .flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN privilege. */
	  .policy = bf_gdt_policy,
	  .doit = ovs_bf_gdt_cmd_del
	},
    { .cmd = OVS_BF_GDT_CMD_GET,
	  .flags = 0,		    /* OK for unprivileged users. */
	  .policy = bf_gdt_policy,
	  .doit = ovs_bf_gdt_cmd_get,
	  .dumpit = ovs_bf_gdt_cmd_dump
	},
    { .cmd = OVS_BF_GDT_CMD_SET,
	  .flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN privilege. */
	  .policy = bf_gdt_policy,
	  .doit = ovs_bf_gdt_cmd_new_or_set
	}
};

#endif //end #ifdef LC_ENABLE

int ovs_dp_upcall(struct datapath *dp, struct sk_buff *skb,
        const struct dp_upcall_info *upcall_info)
{
    struct dp_stats_percpu *stats;
    int dp_ifindex;
    int err;

    if (upcall_info->pid == 0) {
        err = -ENOTCONN;
        goto err;
    }

    dp_ifindex = get_dpifindex(dp);
    if (!dp_ifindex) {
        err = -ENODEV;
        goto err;
    }

    forward_ip_summed(skb, true);

    if (!skb_is_gso(skb))
        err = queue_userspace_packet(ovs_dp_get_net(dp), dp_ifindex, skb, upcall_info);
    else
        err = queue_gso_packets(ovs_dp_get_net(dp), dp_ifindex, skb, upcall_info);
    if (err)
        goto err;

    return 0;

err:
    stats = per_cpu_ptr(dp->stats_percpu, smp_processor_id());

    u64_stats_update_begin(&stats->sync);
    stats->n_lost++;
    u64_stats_update_end(&stats->sync);

    return err;
}

static int queue_gso_packets(struct net *net, int dp_ifindex,
        struct sk_buff *skb,
        const struct dp_upcall_info *upcall_info)
{
    unsigned short gso_type = skb_shinfo(skb)->gso_type;
    struct dp_upcall_info later_info;
    struct sw_flow_key later_key;
    struct sk_buff *segs, *nskb;
    int err;

    segs = skb_gso_segment(skb, NETIF_F_SG | NETIF_F_HW_CSUM);
    if (IS_ERR(segs))
        return PTR_ERR(segs);

    /* Queue all of the segments. */
    skb = segs;
	do {
		err = queue_userspace_packet(net, dp_ifindex, skb, upcall_info);
		if (err)
			break;

		if (skb == segs && gso_type & SKB_GSO_UDP) {
			/* The initial flow key extracted by ovs_flow_extract()
			 * in this case is for a first fragment, so we need to
			 * properly mark later fragments.
			 */
			later_key = *upcall_info->key;
			later_key.ip.frag = OVS_FRAG_TYPE_LATER;

			later_info = *upcall_info;
			later_info.key = &later_key;
			upcall_info = &later_info;
		}
	} while ((skb = skb->next));

	/* Free all of the segments. */
	skb = segs;
	do {
		nskb = skb->next;
		if (err)
			kfree_skb(skb);
		else
			consume_skb(skb);
	} while ((skb = nskb));
	return err;
}

static int queue_userspace_packet(struct net *net, int dp_ifindex,
				  struct sk_buff *skb,
				  const struct dp_upcall_info *upcall_info)
{
	struct ovs_header *upcall;
	struct sk_buff *nskb = NULL;
	struct sk_buff *user_skb; /* to be queued to userspace */
	struct nlattr *nla;
	unsigned int len;
	int err;

	if (vlan_tx_tag_present(skb)) {
		nskb = skb_clone(skb, GFP_ATOMIC);
		if (!nskb)
			return -ENOMEM;
		
		err = vlan_deaccel_tag(nskb);
		if (err)
			return err;

		skb = nskb;
	}

	if (nla_attr_size(skb->len) > USHRT_MAX) {
		err = -EFBIG;
		goto out;
	}

	len = sizeof(struct ovs_header);
	len += nla_total_size(skb->len);
	len += nla_total_size(FLOW_BUFSIZE);
	if (upcall_info->cmd == OVS_PACKET_CMD_ACTION)
		len += nla_total_size(8);

	user_skb = genlmsg_new(len, GFP_ATOMIC);
	if (!user_skb) {
		err = -ENOMEM;
		goto out;
	}

	upcall = genlmsg_put(user_skb, 0, 0, &dp_packet_genl_family,
			     0, upcall_info->cmd);
	upcall->dp_ifindex = dp_ifindex;

	nla = nla_nest_start(user_skb, OVS_PACKET_ATTR_KEY);
	ovs_flow_to_nlattrs(upcall_info->key, user_skb);
	nla_nest_end(user_skb, nla);

	if (upcall_info->userdata)
		nla_put_u64(user_skb, OVS_PACKET_ATTR_USERDATA,
			    nla_get_u64(upcall_info->userdata));

	nla = __nla_reserve(user_skb, OVS_PACKET_ATTR_PACKET, skb->len);

	skb_copy_and_csum_dev(skb, nla_data(nla));

	err = genlmsg_unicast(net, user_skb, upcall_info->pid);

out:
	kfree_skb(nskb);
	return err;
}

/* Called with genl_mutex. */
static int flush_flows(struct datapath *dp)
{
	struct flow_table *old_table;
	struct flow_table *new_table;

	old_table = genl_dereference(dp->table);
	new_table = ovs_flow_tbl_alloc(TBL_MIN_BUCKETS);
	if (!new_table)
		return -ENOMEM;

	rcu_assign_pointer(dp->table, new_table);

	ovs_flow_tbl_deferred_destroy(old_table);
	return 0;
}

static int validate_actions(const struct nlattr *attr,
				const struct sw_flow_key *key, int depth);

static int validate_sample(const struct nlattr *attr,
				const struct sw_flow_key *key, int depth)
{
	const struct nlattr *attrs[OVS_SAMPLE_ATTR_MAX + 1];
	const struct nlattr *probability, *actions;
	const struct nlattr *a;
	int rem;

	memset(attrs, 0, sizeof(attrs));
	nla_for_each_nested(a, attr, rem) {
		int type = nla_type(a);
		if (!type || type > OVS_SAMPLE_ATTR_MAX || attrs[type])
			return -EINVAL;
		attrs[type] = a;
	}
	if (rem)
		return -EINVAL;

	probability = attrs[OVS_SAMPLE_ATTR_PROBABILITY];
	if (!probability || nla_len(probability) != sizeof(u32))
		return -EINVAL;

	actions = attrs[OVS_SAMPLE_ATTR_ACTIONS];
	if (!actions || (nla_len(actions) && nla_len(actions) < NLA_HDRLEN))
		return -EINVAL;
	return validate_actions(actions, key, depth + 1);
}

static int validate_tp_port(const struct sw_flow_key *flow_key)
{
	if (flow_key->eth.type == htons(ETH_P_IP)) {
		if (flow_key->ipv4.tp.src || flow_key->ipv4.tp.dst)
			return 0;
	} else if (flow_key->eth.type == htons(ETH_P_IPV6)) {
		if (flow_key->ipv6.tp.src || flow_key->ipv6.tp.dst)
			return 0;
	}

	return -EINVAL;
}

static int validate_set(const struct nlattr *a,
			const struct sw_flow_key *flow_key)
{
	const struct nlattr *ovs_key = nla_data(a);
	int key_type = nla_type(ovs_key);

	/* There can be only one key in a action */
	if (nla_total_size(nla_len(ovs_key)) != nla_len(a))
		return -EINVAL;

	if (key_type > OVS_KEY_ATTR_MAX ||
	    nla_len(ovs_key) != ovs_key_lens[key_type])
		return -EINVAL;

	switch (key_type) {
	const struct ovs_key_ipv4 *ipv4_key;

	case OVS_KEY_ATTR_PRIORITY:
	case OVS_KEY_ATTR_TUN_ID:
	case OVS_KEY_ATTR_ETHERNET:
		break;

	case OVS_KEY_ATTR_IPV4:
		if (flow_key->eth.type != htons(ETH_P_IP))
			return -EINVAL;

		if (!flow_key->ip.proto)
			return -EINVAL;

		ipv4_key = nla_data(ovs_key);
		if (ipv4_key->ipv4_proto != flow_key->ip.proto)
			return -EINVAL;

		if (ipv4_key->ipv4_frag != flow_key->ip.frag)
			return -EINVAL;

		break;

	case OVS_KEY_ATTR_TCP:
		if (flow_key->ip.proto != IPPROTO_TCP)
			return -EINVAL;

		return validate_tp_port(flow_key);

	case OVS_KEY_ATTR_UDP:
		if (flow_key->ip.proto != IPPROTO_UDP)
			return -EINVAL;

		return validate_tp_port(flow_key);

	default:
		return -EINVAL;
	}

	return 0;
}

static int validate_userspace(const struct nlattr *attr)
{
	static const struct nla_policy userspace_policy[OVS_USERSPACE_ATTR_MAX + 1] =	{
		[OVS_USERSPACE_ATTR_PID] = {.type = NLA_U32 },
		[OVS_USERSPACE_ATTR_USERDATA] = {.type = NLA_U64 },
	};
	struct nlattr *a[OVS_USERSPACE_ATTR_MAX + 1];
	int error;

	error = nla_parse_nested(a, OVS_USERSPACE_ATTR_MAX,
				 attr, userspace_policy);
	if (error)
		return error;

	if (!a[OVS_USERSPACE_ATTR_PID] ||
	    !nla_get_u32(a[OVS_USERSPACE_ATTR_PID]))
		return -EINVAL;

	return 0;
}

static int validate_actions(const struct nlattr *attr,
				const struct sw_flow_key *key,  int depth)
{
	const struct nlattr *a;
	int rem, err;

	if (depth >= SAMPLE_ACTION_DEPTH)
		return -EOVERFLOW;

	nla_for_each_nested(a, attr, rem) {
		/* Expected argument lengths, (u32)-1 for variable length. */
		static const u32 action_lens[OVS_ACTION_ATTR_MAX + 1] = {
			[OVS_ACTION_ATTR_OUTPUT] = sizeof(u32),
#ifdef LC_ENABLE
			[OVS_ACTION_ATTR_REMOTE] = sizeof(u64),
#endif
			[OVS_ACTION_ATTR_USERSPACE] = (u32)-1,
			[OVS_ACTION_ATTR_PUSH_VLAN] = sizeof(struct ovs_action_push_vlan),
			[OVS_ACTION_ATTR_POP_VLAN] = 0,
			[OVS_ACTION_ATTR_SET] = (u32)-1,
			[OVS_ACTION_ATTR_SAMPLE] = (u32)-1
		};
		const struct ovs_action_push_vlan *vlan;
		int type = nla_type(a);

		if (type > OVS_ACTION_ATTR_MAX ||
		    (action_lens[type] != nla_len(a) &&
		     action_lens[type] != (u32)-1))
			return -EINVAL;

		switch (type) {
		case OVS_ACTION_ATTR_UNSPEC:
			return -EINVAL;

		case OVS_ACTION_ATTR_USERSPACE:
			err = validate_userspace(a);
			if (err)
				return err;
			break;

		case OVS_ACTION_ATTR_OUTPUT:
			if (nla_get_u32(a) >= DP_MAX_PORTS)
				return -EINVAL;
			break;

#ifdef LC_ENABLE
		case OVS_ACTION_ATTR_REMOTE:
			if (((nla_get_u64(a)>>32)&0xffffffff) >= DP_MAX_PORTS)
				return -EINVAL;
			break;
#endif

		case OVS_ACTION_ATTR_POP_VLAN:
			break;

		case OVS_ACTION_ATTR_PUSH_VLAN:
			vlan = nla_data(a);
			if (vlan->vlan_tpid != htons(ETH_P_8021Q))
				return -EINVAL;
			if (!(vlan->vlan_tci & htons(VLAN_TAG_PRESENT)))
				return -EINVAL;
			break;

		case OVS_ACTION_ATTR_SET:
			err = validate_set(a, key);
			if (err)
				return err;
			break;

		case OVS_ACTION_ATTR_SAMPLE:
			err = validate_sample(a, key, depth);
			if (err)
				return err;
			break;

		default:
			return -EINVAL;
		}
	}

	if (rem > 0)
		return -EINVAL;

	return 0;
}

static void clear_stats(struct sw_flow *flow)
{
	flow->used = 0;
	flow->tcp_flags = 0;
	flow->packet_count = 0;
	flow->byte_count = 0;
}

static int ovs_packet_cmd_execute(struct sk_buff *skb, struct genl_info *info)
{
#ifdef DEBUG
    pr_info("ovs_packet_cmd_execute(): receive pkt_execute cmd from ovsd\n");
#endif
	struct ovs_header *ovs_header = info->userhdr;
	struct nlattr **a = info->attrs;
	struct sw_flow_actions *acts;
	struct sk_buff *packet;
	struct sw_flow *flow;
	struct datapath *dp;
	struct ethhdr *eth;
	int len;
	int err;
	int key_len;

	err = -EINVAL;
	if (!a[OVS_PACKET_ATTR_PACKET] || !a[OVS_PACKET_ATTR_KEY] ||
	    !a[OVS_PACKET_ATTR_ACTIONS] ||
	    nla_len(a[OVS_PACKET_ATTR_PACKET]) < ETH_HLEN)
		goto err;

	len = nla_len(a[OVS_PACKET_ATTR_PACKET]);
#ifdef DEBUG
    pr_info("ovs_packet_cmd_execute(),len=%u",len);
#endif
	packet = __dev_alloc_skb(NET_IP_ALIGN + len, GFP_KERNEL);
	err = -ENOMEM;
	if (!packet)
		goto err;
	skb_reserve(packet, NET_IP_ALIGN);

	memcpy(__skb_put(packet, len), nla_data(a[OVS_PACKET_ATTR_PACKET]), len);

	skb_reset_mac_header(packet);
	eth = eth_hdr(packet);
#ifdef DEBUG
    pr_mac("ovs_packet_cmd_execute()",eth->h_source,eth->h_dest,ntohs(eth->h_proto));
#endif

	/* Normally, setting the skb 'protocol' field would be handled by a
	 * call to eth_type_trans(), but it assumes there's a sending
	 * device, which we may not have. */
	if (ntohs(eth->h_proto) >= 1536)
		packet->protocol = eth->h_proto;
	else
		packet->protocol = htons(ETH_P_802_2);

	/* Build an sw_flow for sending this packet. */
	flow = ovs_flow_alloc();
	err = PTR_ERR(flow);
	if (IS_ERR(flow))
		goto err_kfree_skb;

	err = ovs_flow_extract(packet, -1, &flow->key, &key_len);
	if (err)
		goto err_flow_put;

	err = ovs_flow_metadata_from_nlattrs(&flow->key.phy.priority,
					     &flow->key.phy.in_port,
					     &flow->key.phy.tun_id,
					     a[OVS_PACKET_ATTR_KEY]);
	if (err)
		goto err_flow_put;

	err = validate_actions(a[OVS_PACKET_ATTR_ACTIONS], &flow->key, 0);
	if (err)
		goto err_flow_put;

	flow->hash = ovs_flow_hash(&flow->key, key_len);

	acts = ovs_flow_actions_alloc(a[OVS_PACKET_ATTR_ACTIONS]);
#ifdef DEBUG
    pr_mac("ovs_packet_cmd_execute()",eth->h_source,eth->h_dest,ntohs(eth->h_proto));
    switch (acts->actions[0].nla_type) {
        case OVS_ACTION_ATTR_OUTPUT:
            pr_info("action = OUTPUT to port %u\n", nla_get_u32(acts->actions));
            break;
        case OVS_ACTION_ATTR_USERSPACE:
            pr_info("action = OUTPUT to userspace\n");
            break;
        case OVS_ACTION_ATTR_REMOTE:
            pr_info("action = REMOTE, port=%u, ip=0x%x\n",(unsigned int)((nla_get_u64(acts->actions)>>32)&0xffffffff),(unsigned int)(nla_get_u64(acts->actions)&0xffffffff));
            break;
        default:
            pr_info("unknown action type %u\n",acts->actions[0].nla_type);
    }
#endif
	err = PTR_ERR(acts);
	if (IS_ERR(acts))
		goto err_flow_put;
	rcu_assign_pointer(flow->sf_acts, acts);

	OVS_CB(packet)->flow = flow;
	packet->priority = flow->key.phy.priority;

	rcu_read_lock();
	dp = get_dp(sock_net(skb->sk), ovs_header->dp_ifindex);
	err = -ENODEV;
	if (!dp)
		goto err_unlock;

	local_bh_disable();
	err = ovs_execute_actions(dp, packet);
	local_bh_enable();
	rcu_read_unlock();

	ovs_flow_put(flow);
	return err;

err_unlock:
	rcu_read_unlock();
err_flow_put:
	ovs_flow_put(flow);
err_kfree_skb:
	kfree_skb(packet);
err:
	return err;
}

static const struct nla_policy packet_policy[OVS_PACKET_ATTR_MAX + 1] = {
	[OVS_PACKET_ATTR_PACKET] = { .type = NLA_UNSPEC },
	[OVS_PACKET_ATTR_KEY] = { .type = NLA_NESTED },
	[OVS_PACKET_ATTR_ACTIONS] = { .type = NLA_UNSPEC },
};

static struct genl_ops dp_packet_genl_ops[] = {
	{ .cmd = OVS_PACKET_CMD_EXECUTE,
	  .flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN privilege. */
	  .policy = packet_policy,
	  .doit = ovs_packet_cmd_execute
	}
};

static void get_dp_stats(struct datapath *dp, struct ovs_dp_stats *stats)
{
	int i;
	struct flow_table *table = genl_dereference(dp->table);

	stats->n_flows = ovs_flow_tbl_count(table);

	stats->n_hit = stats->n_missed = stats->n_lost = 0;
	for_each_possible_cpu(i) {
		const struct dp_stats_percpu *percpu_stats;
		struct dp_stats_percpu local_stats;
		unsigned int start;

		percpu_stats = per_cpu_ptr(dp->stats_percpu, i);

		do {
			start = u64_stats_fetch_begin_bh(&percpu_stats->sync);
			local_stats = *percpu_stats;
		} while (u64_stats_fetch_retry_bh(&percpu_stats->sync, start));

		stats->n_hit += local_stats.n_hit;
		stats->n_missed += local_stats.n_missed;
		stats->n_lost += local_stats.n_lost;
	}
}

static const struct nla_policy flow_policy[OVS_FLOW_ATTR_MAX + 1] = {
	[OVS_FLOW_ATTR_KEY] = { .type = NLA_NESTED },
	[OVS_FLOW_ATTR_ACTIONS] = { .type = NLA_NESTED },
	[OVS_FLOW_ATTR_CLEAR] = { .type = NLA_FLAG },
};

static struct genl_family dp_flow_genl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = sizeof(struct ovs_header),
	.name = OVS_FLOW_FAMILY,
	.version = OVS_FLOW_VERSION,
	.maxattr = OVS_FLOW_ATTR_MAX,
	 SET_NETNSOK
};

static struct genl_multicast_group ovs_dp_flow_multicast_group = {
	.name = OVS_FLOW_MCGROUP
};

/* Called with genl_lock. */
static int ovs_flow_cmd_fill_info(struct sw_flow *flow, struct datapath *dp,
				  struct sk_buff *skb, u32 pid,
				  u32 seq, u32 flags, u8 cmd)
{
	const int skb_orig_len = skb->len;
	const struct sw_flow_actions *sf_acts;
	struct ovs_flow_stats stats;
	struct ovs_header *ovs_header;
	struct nlattr *nla;
	unsigned long used;
	u8 tcp_flags;
	int err;

	sf_acts = rcu_dereference_protected(flow->sf_acts,
					    lockdep_genl_is_held());

	ovs_header = genlmsg_put(skb, pid, seq, &dp_flow_genl_family, flags, cmd);
	if (!ovs_header)
		return -EMSGSIZE;

	ovs_header->dp_ifindex = get_dpifindex(dp);

	nla = nla_nest_start(skb, OVS_FLOW_ATTR_KEY);
	if (!nla)
		goto nla_put_failure;
	err = ovs_flow_to_nlattrs(&flow->key, skb);
	if (err)
		goto error;
	nla_nest_end(skb, nla);

	spin_lock_bh(&flow->lock);
	used = flow->used;
	stats.n_packets = flow->packet_count;
	stats.n_bytes = flow->byte_count;
	tcp_flags = flow->tcp_flags;
	spin_unlock_bh(&flow->lock);

	if (used &&
	    nla_put_u64(skb, OVS_FLOW_ATTR_USED, ovs_flow_used_time(used)))
		goto nla_put_failure;

	if (stats.n_packets &&
	    nla_put(skb, OVS_FLOW_ATTR_STATS,
		    sizeof(struct ovs_flow_stats), &stats))
		goto nla_put_failure;

	if (tcp_flags &&
	    nla_put_u8(skb, OVS_FLOW_ATTR_TCP_FLAGS, tcp_flags))
		goto nla_put_failure;

	/* If OVS_FLOW_ATTR_ACTIONS doesn't fit, skip dumping the actions if
	 * this is the first flow to be dumped into 'skb'.  This is unusual for
	 * Netlink but individual action lists can be longer than
	 * NLMSG_GOODSIZE and thus entirely undumpable if we didn't do this.
	 * The userspace caller can always fetch the actions separately if it
	 * really wants them.  (Most userspace callers in fact don't care.)
	 *
	 * This can only fail for dump operations because the skb is always
	 * properly sized for single flows.
	 */
	err = nla_put(skb, OVS_FLOW_ATTR_ACTIONS, sf_acts->actions_len,
		      sf_acts->actions);
	if (err < 0 && skb_orig_len)
		goto error;

	return genlmsg_end(skb, ovs_header);

nla_put_failure:
	err = -EMSGSIZE;
error:
	genlmsg_cancel(skb, ovs_header);
	return err;
}

static struct sk_buff *ovs_flow_cmd_alloc_info(struct sw_flow *flow)
{
	const struct sw_flow_actions *sf_acts;
	int len;

	sf_acts = rcu_dereference_protected(flow->sf_acts,
					    lockdep_genl_is_held());

	/* OVS_FLOW_ATTR_KEY */
	len = nla_total_size(FLOW_BUFSIZE);
	/* OVS_FLOW_ATTR_ACTIONS */
	len += nla_total_size(sf_acts->actions_len);
	/* OVS_FLOW_ATTR_STATS */
	len += nla_total_size(sizeof(struct ovs_flow_stats));
	/* OVS_FLOW_ATTR_TCP_FLAGS */
	len += nla_total_size(1);
	/* OVS_FLOW_ATTR_USED */
	len += nla_total_size(8);

	len += NLMSG_ALIGN(sizeof(struct ovs_header));

	return genlmsg_new(len, GFP_KERNEL);
}

static struct sk_buff *ovs_flow_cmd_build_info(struct sw_flow *flow,
					       struct datapath *dp,
					       u32 pid, u32 seq, u8 cmd)
{
	struct sk_buff *skb;
	int retval;

	skb = ovs_flow_cmd_alloc_info(flow);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	retval = ovs_flow_cmd_fill_info(flow, dp, skb, pid, seq, 0, cmd);
	BUG_ON(retval < 0);
	return skb;
}

static int ovs_flow_cmd_new_or_set(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **a = info->attrs;
	struct ovs_header *ovs_header = info->userhdr;
	struct sw_flow_key key;
	struct sw_flow *flow;
	struct sk_buff *reply;
	struct datapath *dp;
	struct flow_table *table;
	int error;
	int key_len;

	/* Extract key. */
	error = -EINVAL;
	if (!a[OVS_FLOW_ATTR_KEY])
		goto error;
	error = ovs_flow_from_nlattrs(&key, &key_len, a[OVS_FLOW_ATTR_KEY]);
	if (error)
		goto error;

#ifdef DEBUG
	if (info->genlhdr->cmd == OVS_FLOW_CMD_NEW) {
        pr_mac("ovs_flow_cmd_new_or_set(): dp receive flow_new cmd",key.eth.src,key.eth.dst,ntohs(key.eth.type));
    }else if (info->genlhdr->cmd == OVS_FLOW_CMD_SET) {
        pr_mac("ovs_flow_cmd_new_or_set(): dp receive flow_set cmd",key.eth.src,key.eth.dst,ntohs(key.eth.type));
    } else {
        pr_info("ovs_flow_cmd_new_or_set(): dp receive INVALID flow_new_or_set cmd.\n");
    }
#endif

	/* Validate actions. */
	if (a[OVS_FLOW_ATTR_ACTIONS]) {
		error = validate_actions(a[OVS_FLOW_ATTR_ACTIONS], &key,  0);
		if (error)
			goto error;
	} else if (info->genlhdr->cmd == OVS_FLOW_CMD_NEW) {
		error = -EINVAL;
		goto error;
	}

	dp = get_dp(sock_net(skb->sk), ovs_header->dp_ifindex);
	error = -ENODEV;
	if (!dp)
		goto error;

	table = genl_dereference(dp->table);
	flow = ovs_flow_tbl_lookup(table, &key, key_len);
	if (!flow) { //no same flow existed yet
#ifdef DEBUG
        pr_info("No same flow in local tbl yet, will add.");
#endif
		struct sw_flow_actions *acts;

		/* Bail out if we're not allowed to create a new flow. */
		error = -ENOENT;
		if (info->genlhdr->cmd == OVS_FLOW_CMD_SET)
			goto error;

		/* Expand table, if necessary, to make room. */
		if (ovs_flow_tbl_need_to_expand(table)) {
			struct flow_table *new_table;

			new_table = ovs_flow_tbl_expand(table);
			if (!IS_ERR(new_table)) {
				rcu_assign_pointer(dp->table, new_table);
				ovs_flow_tbl_deferred_destroy(table);
				table = genl_dereference(dp->table);
			}
		}

		/* Allocate flow. */
		flow = ovs_flow_alloc();
		if (IS_ERR(flow)) {
			error = PTR_ERR(flow);
			goto error;
		}
		flow->key = key;
		clear_stats(flow);

		/* Obtain actions. */
		acts = ovs_flow_actions_alloc(a[OVS_FLOW_ATTR_ACTIONS]);
#ifdef DEBUG
        pr_info("actions_len=%u",acts->actions_len);
#endif
#ifdef DEBUG
        switch (acts->actions[0].nla_type) {
            case OVS_ACTION_ATTR_OUTPUT:
                pr_info("action = OUTPUT to port %u\n", nla_get_u32(acts->actions));
                break;
            case OVS_ACTION_ATTR_USERSPACE:
                pr_info("action = OUTPUT to userspace\n");
                break;
            case OVS_ACTION_ATTR_REMOTE:
                pr_info("action = REMOTE, port=%u, ip=0x%x\n",(unsigned int)((nla_get_u64(acts->actions)>>32)&0xffffffff),(unsigned int)(nla_get_u64(acts->actions)&0xffffffff));
                break;
            default:
                pr_info("unknown action type %u\n",acts->actions[0].nla_type);
        }
#endif
        error = PTR_ERR(acts);
        if (IS_ERR(acts))
            goto error_free_flow;
        rcu_assign_pointer(flow->sf_acts, acts);

        /* Put flow in bucket. */
        flow->hash = ovs_flow_hash(&key, key_len);
#ifdef DEBUG
        pr_info("will put flow in bucket, flow->hash=0x%x.",flow->hash);
#endif
        ovs_flow_tbl_insert(table, flow);

        reply = ovs_flow_cmd_build_info(flow, dp, info->snd_pid,
                info->snd_seq,
                OVS_FLOW_CMD_NEW);
    } else {
#ifdef DEBUG
        pr_info("Same flow in local tbl already, will update.");
#endif
        /* We found a matching flow. */
        struct sw_flow_actions *old_acts;
        struct nlattr *acts_attrs;

        /* Bail out if we're not allowed to modify an existing flow.
         * We accept NLM_F_CREATE in place of the intended NLM_F_EXCL
         * because Generic Netlink treats the latter as a dump
         * request.  We also accept NLM_F_EXCL in case that bug ever
         * gets fixed.
         */
        error = -EEXIST;
        if (info->genlhdr->cmd == OVS_FLOW_CMD_NEW &&
                info->nlhdr->nlmsg_flags & (NLM_F_CREATE | NLM_F_EXCL))
            goto error;

        /* Update actions. */
        old_acts = rcu_dereference_protected(flow->sf_acts,
                lockdep_genl_is_held());
        acts_attrs = a[OVS_FLOW_ATTR_ACTIONS];
        if (acts_attrs &&
                (old_acts->actions_len != nla_len(acts_attrs) ||
                 memcmp(old_acts->actions, nla_data(acts_attrs),
                     old_acts->actions_len))) {
            struct sw_flow_actions *new_acts;

            new_acts = ovs_flow_actions_alloc(acts_attrs);
#ifdef DEBUG
        pr_info("actions_len=%u",new_acts->actions_len);
#endif

            error = PTR_ERR(new_acts);
            if (IS_ERR(new_acts))
                goto error;
#ifdef DEBUG
        switch (new_acts->actions[0].nla_type) {
            case OVS_ACTION_ATTR_OUTPUT:
                pr_info("action = OUTPUT to port %u\n", nla_get_u32(new_acts->actions));
                break;
            case OVS_ACTION_ATTR_USERSPACE:
                pr_info("action = OUTPUT to userspace\n");
                break;
            case OVS_ACTION_ATTR_REMOTE:
                pr_info("action = REMOTE, port=%u, ip=0x%x\n",(unsigned int)((nla_get_u64(new_acts->actions)>>32)&0xffffffff),(unsigned int)(nla_get_u64(new_acts->actions)&0xffffffff));
                break;
            default:
                pr_info("unknown action type %u\n",new_acts->actions[0].nla_type);
        }
#endif

#ifdef DEBUG
        pr_info("will set flow to new_act, flow->hash=0x%x",flow->hash);
#endif
            rcu_assign_pointer(flow->sf_acts, new_acts);
            ovs_flow_deferred_free_acts(old_acts);
        }

        reply = ovs_flow_cmd_build_info(flow, dp, info->snd_pid,
                info->snd_seq, OVS_FLOW_CMD_NEW);

        /* Clear stats. */
        if (a[OVS_FLOW_ATTR_CLEAR]) {
            spin_lock_bh(&flow->lock);
            clear_stats(flow);
            spin_unlock_bh(&flow->lock);
        }
    }

    if (!IS_ERR(reply))
        genl_notify(reply, genl_info_net(info), info->snd_pid,
                ovs_dp_flow_multicast_group.id, info->nlhdr,
                GFP_KERNEL);
    else
        netlink_set_err(GENL_SOCK(sock_net(skb->sk)), 0,
                ovs_dp_flow_multicast_group.id,	PTR_ERR(reply));
    return 0;

error_free_flow:
    ovs_flow_put(flow);
error:
    return error;
}

static int ovs_flow_cmd_get(struct sk_buff *skb, struct genl_info *info)
{
    struct nlattr **a = info->attrs;
    struct ovs_header *ovs_header = info->userhdr;
    struct sw_flow_key key;
    struct sk_buff *reply;
    struct sw_flow *flow;
    struct datapath *dp;
    struct flow_table *table;
    int err;
    int key_len;

	if (!a[OVS_FLOW_ATTR_KEY])
		return -EINVAL;
	err = ovs_flow_from_nlattrs(&key, &key_len, a[OVS_FLOW_ATTR_KEY]);
	if (err)
		return err;

	dp = get_dp(sock_net(skb->sk), ovs_header->dp_ifindex);
	if (!dp)
		return -ENODEV;

	table = genl_dereference(dp->table);
	flow = ovs_flow_tbl_lookup(table, &key, key_len);
	if (!flow)
		return -ENOENT;

	reply = ovs_flow_cmd_build_info(flow, dp, info->snd_pid,
					info->snd_seq, OVS_FLOW_CMD_NEW);
	if (IS_ERR(reply))
		return PTR_ERR(reply);

	return genlmsg_reply(reply, info);
}

static int ovs_flow_cmd_del(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **a = info->attrs;
	struct ovs_header *ovs_header = info->userhdr;
	struct sw_flow_key key;
	struct sk_buff *reply;
	struct sw_flow *flow;
	struct datapath *dp;
	struct flow_table *table;
	int err;
	int key_len;

	dp = get_dp(sock_net(skb->sk), ovs_header->dp_ifindex);
	if (!dp)
		return -ENODEV;

	if (!a[OVS_FLOW_ATTR_KEY])
		return flush_flows(dp);

	err = ovs_flow_from_nlattrs(&key, &key_len, a[OVS_FLOW_ATTR_KEY]);
	if (err)
		return err;

	table = genl_dereference(dp->table);
	flow = ovs_flow_tbl_lookup(table, &key, key_len);
	if (!flow)
		return -ENOENT;

#ifdef DEBUG
    pr_info(">>>ovs_flow_cmd_del(): dp receive flow_del cmd");
    pr_key(&key,"del_key");
#endif

	reply = ovs_flow_cmd_alloc_info(flow);
	if (!reply)
		return -ENOMEM;

	ovs_flow_tbl_remove(table, flow);

	err = ovs_flow_cmd_fill_info(flow, dp, reply, info->snd_pid,
				     info->snd_seq, 0, OVS_FLOW_CMD_DEL);
	BUG_ON(err < 0);

	ovs_flow_deferred_free(flow);

	genl_notify(reply, genl_info_net(info), info->snd_pid,
		    ovs_dp_flow_multicast_group.id, info->nlhdr, GFP_KERNEL);
#ifdef DEBUG
    pr_info("<<<ovs_flow_cmd_del()");
#endif
	return 0;
}

static int ovs_flow_cmd_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct ovs_header *ovs_header = genlmsg_data(nlmsg_data(cb->nlh));
	struct datapath *dp;
	struct flow_table *table;

	dp = get_dp(sock_net(skb->sk), ovs_header->dp_ifindex);
	if (!dp)
		return -ENODEV;

	table = genl_dereference(dp->table);

	for (;;) {
		struct sw_flow *flow;
		u32 bucket, obj;

		bucket = cb->args[0];
		obj = cb->args[1];
		flow = ovs_flow_tbl_next(table, &bucket, &obj);
		if (!flow)
			break;

		if (ovs_flow_cmd_fill_info(flow, dp, skb,
					   NETLINK_CB(cb->skb).pid,
					   cb->nlh->nlmsg_seq, NLM_F_MULTI,
					   OVS_FLOW_CMD_NEW) < 0)
			break;

		cb->args[0] = bucket;
		cb->args[1] = obj;
	}
	return skb->len;
}

static struct genl_ops dp_flow_genl_ops[] = {
	{ .cmd = OVS_FLOW_CMD_NEW,
	  .flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN privilege. */
	  .policy = flow_policy,
	  .doit = ovs_flow_cmd_new_or_set
	},
	{ .cmd = OVS_FLOW_CMD_DEL,
	  .flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN privilege. */
	  .policy = flow_policy,
	  .doit = ovs_flow_cmd_del
	},
	{ .cmd = OVS_FLOW_CMD_GET,
	  .flags = 0,		    /* OK for unprivileged users. */
	  .policy = flow_policy,
	  .doit = ovs_flow_cmd_get,
	  .dumpit = ovs_flow_cmd_dump
	},
	{ .cmd = OVS_FLOW_CMD_SET,
	  .flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN privilege. */
	  .policy = flow_policy,
	  .doit = ovs_flow_cmd_new_or_set,
	},
};

static const struct nla_policy datapath_policy[OVS_DP_ATTR_MAX + 1] = {
#ifdef HAVE_NLA_NUL_STRING
	[OVS_DP_ATTR_NAME] = { .type = NLA_NUL_STRING, .len = IFNAMSIZ - 1 },
#endif
	[OVS_DP_ATTR_UPCALL_PID] = { .type = NLA_U32 },
};

static struct genl_family dp_datapath_genl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = sizeof(struct ovs_header),
	.name = OVS_DATAPATH_FAMILY,
	.version = OVS_DATAPATH_VERSION,
	.maxattr = OVS_DP_ATTR_MAX,
	 SET_NETNSOK
};

static struct genl_multicast_group ovs_dp_datapath_multicast_group = {
	.name = OVS_DATAPATH_MCGROUP
};

static int ovs_dp_cmd_fill_info(struct datapath *dp, struct sk_buff *skb,
				u32 pid, u32 seq, u32 flags, u8 cmd)
{
	struct ovs_header *ovs_header;
	struct ovs_dp_stats dp_stats;
	int err;

	ovs_header = genlmsg_put(skb, pid, seq, &dp_datapath_genl_family,
				   flags, cmd);
	if (!ovs_header)
		goto error;

	ovs_header->dp_ifindex = get_dpifindex(dp);

	rcu_read_lock();
	err = nla_put_string(skb, OVS_DP_ATTR_NAME, ovs_dp_name(dp));
	rcu_read_unlock();
	if (err)
		goto nla_put_failure;

	get_dp_stats(dp, &dp_stats);
	if (nla_put(skb, OVS_DP_ATTR_STATS, sizeof(struct ovs_dp_stats), &dp_stats))
		goto nla_put_failure;

	return genlmsg_end(skb, ovs_header);

nla_put_failure:
	genlmsg_cancel(skb, ovs_header);
error:
	return -EMSGSIZE;
}

static struct sk_buff *ovs_dp_cmd_build_info(struct datapath *dp, u32 pid,
					     u32 seq, u8 cmd)
{
	struct sk_buff *skb;
	int retval;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	retval = ovs_dp_cmd_fill_info(dp, skb, pid, seq, 0, cmd);
	if (retval < 0) {
		kfree_skb(skb);
		return ERR_PTR(retval);
	}
	return skb;
}

static int ovs_dp_cmd_validate(struct nlattr *a[OVS_DP_ATTR_MAX + 1])
{
	return CHECK_NUL_STRING(a[OVS_DP_ATTR_NAME], IFNAMSIZ - 1);
}

/* Called with genl_mutex and optionally with RTNL lock also. */
static struct datapath *lookup_datapath(struct net *net,
					struct ovs_header *ovs_header,
					struct nlattr *a[OVS_DP_ATTR_MAX + 1])
{
	struct datapath *dp;

	if (!a[OVS_DP_ATTR_NAME])
		dp = get_dp(net, ovs_header->dp_ifindex);
	else {
		struct vport *vport;

		rcu_read_lock();
		vport = ovs_vport_locate(net, nla_data(a[OVS_DP_ATTR_NAME]));
		dp = vport && vport->port_no == OVSP_LOCAL ? vport->dp : NULL;
		rcu_read_unlock();
	}
	return dp ? dp : ERR_PTR(-ENODEV);
}

static int ovs_dp_cmd_new(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **a = info->attrs;
	struct vport_parms parms;
	struct sk_buff *reply;
	struct datapath *dp;
	struct vport *vport;
	struct ovs_net *ovs_net;
	int err, i;

	err = -EINVAL;
	if (!a[OVS_DP_ATTR_NAME] || !a[OVS_DP_ATTR_UPCALL_PID])
		goto err;

	err = ovs_dp_cmd_validate(a);
	if (err)
		goto err;

	rtnl_lock();

	err = -ENOMEM;
	dp = kzalloc(sizeof(*dp), GFP_KERNEL);
	if (dp == NULL)
		goto err_unlock_rtnl;

	/* Initialize kobject for bridge.  This will be added as
	 * /sys/class/net/<devname>/brif later, if sysfs is enabled. */
	dp->ifobj.kset = NULL;
	kobject_init(&dp->ifobj, &dp_ktype);

	ovs_dp_set_net(dp, hold_net(sock_net(skb->sk)));

	/* Allocate table. */
	err = -ENOMEM;
	rcu_assign_pointer(dp->table, ovs_flow_tbl_alloc(TBL_MIN_BUCKETS));
	if (!dp->table)
		goto err_free_dp;

	dp->stats_percpu = alloc_percpu(struct dp_stats_percpu);
	if (!dp->stats_percpu) {
		err = -ENOMEM;
		goto err_destroy_table;
	}

	dp->ports = kmalloc(DP_VPORT_HASH_BUCKETS * sizeof(struct hlist_head),
			    GFP_KERNEL);
	if (!dp->ports) {
		err = -ENOMEM;
		goto err_destroy_percpu;
	}

	for (i = 0; i < DP_VPORT_HASH_BUCKETS; i++)
		INIT_HLIST_HEAD(&dp->ports[i]);

#ifdef LC_ENABLE /*init the lc_group, TODO*/
    dp->local_ip = LC_LOCAL_EDGE_IP; //ip of network interface bonding on dp.
    unsigned int gid = 0;
    set_fs(get_ds());
    struct file* f_gid = filp_open("/tmp/lc_gid.dat",O_RDONLY,0);
    if(!IS_ERR(f_gid)) {
        loff_t pos = 0;
        mm_segment_t fs = get_fs(); 
        set_fs(KERNEL_DS); 
        unsigned char data[8];
        vfs_read(f_gid, data, sizeof(unsigned int), &pos);
        sscanf(data,"%u",&gid);
        dp->gdt = bf_gdt_init(gid);
        filp_close(f_gid, NULL); 
        set_fs(fs); 
    } else {
        dp->gdt = bf_gdt_init(LC_GROUP_DFT_ID);
    }

	if (!dp->gdt) {
        pr_warning("bf_gdt_init() failed when do ovs_dp_cmd_new()");
		err = -ENOMEM;
		goto err_destroy_percpu;
    }
    if(!bf_gdt_add_filter(dp->gdt,LC_BF_LOCAL_ID,LC_BF_LOCAL_PORT,LC_BF_DFT_LEN)) { /*empty local filter*/
        pr_warning("bf_gdt_add_filter() failed when do ovs_dp_cmd_new()");
		err = -ENOMEM;
		goto err_destroy_percpu;
    }
    /*debug*/
    //bf_gdt_add_filter(dp->gdt,0xc0a83a0a,LC_BF_REMOTE_PORT,LC_BF_DFT_LEN); /*create an empty remote filter*/
    //unsigned char tmp_dst[] = {0x08,0x00,0x27,0xab,0xb6,0xa5};
    //bf_gdt_add_item(dp->gdt,0xc0a83a0a,tmp_dst);
    /*debug*/
#endif

	/* Set up our datapath device. */
	parms.name = nla_data(a[OVS_DP_ATTR_NAME]);
	parms.type = OVS_VPORT_TYPE_INTERNAL;
	parms.options = NULL;
	parms.dp = dp;
	parms.port_no = OVSP_LOCAL;
	parms.upcall_pid = nla_get_u32(a[OVS_DP_ATTR_UPCALL_PID]);

	vport = new_vport(&parms);
	if (IS_ERR(vport)) {
		err = PTR_ERR(vport);
		if (err == -EBUSY)
			err = -EEXIST;

		goto err_destroy_ports_array;
	}

	reply = ovs_dp_cmd_build_info(dp, info->snd_pid,
				      info->snd_seq, OVS_DP_CMD_NEW);
	err = PTR_ERR(reply);
	if (IS_ERR(reply))
		goto err_destroy_local_port;

	ovs_net = net_generic(ovs_dp_get_net(dp), ovs_net_id);
	list_add_tail(&dp->list_node, &ovs_net->dps);
	ovs_dp_sysfs_add_dp(dp);

	rtnl_unlock();

	genl_notify(reply, genl_info_net(info), info->snd_pid,
		    ovs_dp_datapath_multicast_group.id, info->nlhdr,
		    GFP_KERNEL);
	return 0;

err_destroy_local_port:
	ovs_dp_detach_port(ovs_vport_rtnl(dp, OVSP_LOCAL));
err_destroy_ports_array:
	kfree(dp->ports);
err_destroy_percpu:
	free_percpu(dp->stats_percpu);
err_destroy_table:
	ovs_flow_tbl_destroy(genl_dereference(dp->table));
err_free_dp:
	release_net(ovs_dp_get_net(dp));
	kfree(dp);
err_unlock_rtnl:
	rtnl_unlock();
err:
	return err;
}

/* Called with genl_mutex. */
static void __dp_destroy(struct datapath *dp)
{
	int i;

	rtnl_lock();

	for (i = 0; i < DP_VPORT_HASH_BUCKETS; i++) {
		struct vport *vport;
		struct hlist_node *node, *n;

		hlist_for_each_entry_safe(vport, node, n, &dp->ports[i], dp_hash_node)
			if (vport->port_no != OVSP_LOCAL)
				ovs_dp_detach_port(vport);
	}

	ovs_dp_sysfs_del_dp(dp);
	list_del(&dp->list_node);
	ovs_dp_detach_port(ovs_vport_rtnl(dp, OVSP_LOCAL));

	/* rtnl_unlock() will wait until all the references to devices that
	 * are pending unregistration have been dropped.  We do it here to
	 * ensure that any internal devices (which contain DP pointers) are
	 * fully destroyed before freeing the datapath.
	 */
	rtnl_unlock();

	call_rcu(&dp->rcu, destroy_dp_rcu);
}

static int ovs_dp_cmd_del(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *reply;
	struct datapath *dp;
	int err;

	err = ovs_dp_cmd_validate(info->attrs);
	if (err)
		return err;

	dp = lookup_datapath(sock_net(skb->sk), info->userhdr, info->attrs);
	err = PTR_ERR(dp);
	if (IS_ERR(dp))
		return err;

	reply = ovs_dp_cmd_build_info(dp, info->snd_pid,
				      info->snd_seq, OVS_DP_CMD_DEL);
	err = PTR_ERR(reply);
	if (IS_ERR(reply))
		return err;

	__dp_destroy(dp);

#ifdef LC_ENABLE /*destroy the bf-gdt*/
    bf_gdt_destroy(dp->gdt);
#endif

	genl_notify(reply, genl_info_net(info), info->snd_pid,
		    ovs_dp_datapath_multicast_group.id, info->nlhdr,
		    GFP_KERNEL);

	return 0;
}

static int ovs_dp_cmd_set(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *reply;
	struct datapath *dp;
	int err;

	err = ovs_dp_cmd_validate(info->attrs);
	if (err)
		return err;

	dp = lookup_datapath(sock_net(skb->sk), info->userhdr, info->attrs);
	if (IS_ERR(dp))
		return PTR_ERR(dp);

	reply = ovs_dp_cmd_build_info(dp, info->snd_pid,
				      info->snd_seq, OVS_DP_CMD_NEW);
	if (IS_ERR(reply)) {
		err = PTR_ERR(reply);
		netlink_set_err(GENL_SOCK(sock_net(skb->sk)), 0,
				ovs_dp_datapath_multicast_group.id, err);
		return 0;
	}

	genl_notify(reply, genl_info_net(info), info->snd_pid,
		    ovs_dp_datapath_multicast_group.id, info->nlhdr,
		    GFP_KERNEL);

	return 0;
}

static int ovs_dp_cmd_get(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *reply;
	struct datapath *dp;
	int err;

	err = ovs_dp_cmd_validate(info->attrs);
	if (err)
		return err;

	dp = lookup_datapath(sock_net(skb->sk), info->userhdr, info->attrs);
	if (IS_ERR(dp))
		return PTR_ERR(dp);

	reply = ovs_dp_cmd_build_info(dp, info->snd_pid,
				      info->snd_seq, OVS_DP_CMD_NEW);
	if (IS_ERR(reply))
		return PTR_ERR(reply);

	return genlmsg_reply(reply, info);
}

static int ovs_dp_cmd_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct ovs_net *ovs_net = net_generic(sock_net(skb->sk), ovs_net_id);
	struct datapath *dp;
	int skip = cb->args[0];
	int i = 0;

	list_for_each_entry(dp, &ovs_net->dps, list_node) {
		if (i >= skip &&
		    ovs_dp_cmd_fill_info(dp, skb, NETLINK_CB(cb->skb).pid,
					 cb->nlh->nlmsg_seq, NLM_F_MULTI,
					 OVS_DP_CMD_NEW) < 0)
			break;
		i++;
	}

	cb->args[0] = i;

	return skb->len;
}

static struct genl_ops dp_datapath_genl_ops[] = {
	{ .cmd = OVS_DP_CMD_NEW,
	  .flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN privilege. */
	  .policy = datapath_policy,
	  .doit = ovs_dp_cmd_new
	},
	{ .cmd = OVS_DP_CMD_DEL,
	  .flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN privilege. */
	  .policy = datapath_policy,
	  .doit = ovs_dp_cmd_del
	},
	{ .cmd = OVS_DP_CMD_GET,
	  .flags = 0,		    /* OK for unprivileged users. */
	  .policy = datapath_policy,
	  .doit = ovs_dp_cmd_get,
	  .dumpit = ovs_dp_cmd_dump
	},
	{ .cmd = OVS_DP_CMD_SET,
	  .flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN privilege. */
	  .policy = datapath_policy,
	  .doit = ovs_dp_cmd_set,
	},
};

static const struct nla_policy vport_policy[OVS_VPORT_ATTR_MAX + 1] = {
#ifdef HAVE_NLA_NUL_STRING
	[OVS_VPORT_ATTR_NAME] = { .type = NLA_NUL_STRING, .len = IFNAMSIZ - 1 },
	[OVS_VPORT_ATTR_STATS] = { .len = sizeof(struct ovs_vport_stats) },
	[OVS_VPORT_ATTR_ADDRESS] = { .len = ETH_ALEN },
#else
	[OVS_VPORT_ATTR_STATS] = { .minlen = sizeof(struct ovs_vport_stats) },
	[OVS_VPORT_ATTR_ADDRESS] = { .minlen = ETH_ALEN },
#endif
	[OVS_VPORT_ATTR_PORT_NO] = { .type = NLA_U32 },
	[OVS_VPORT_ATTR_TYPE] = { .type = NLA_U32 },
	[OVS_VPORT_ATTR_UPCALL_PID] = { .type = NLA_U32 },
	[OVS_VPORT_ATTR_OPTIONS] = { .type = NLA_NESTED },
};

static struct genl_family dp_vport_genl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = sizeof(struct ovs_header),
	.name = OVS_VPORT_FAMILY,
	.version = OVS_VPORT_VERSION,
	.maxattr = OVS_VPORT_ATTR_MAX,
	 SET_NETNSOK
};

struct genl_multicast_group ovs_dp_vport_multicast_group = {
	.name = OVS_VPORT_MCGROUP
};

/* Called with RTNL lock or RCU read lock. */
static int ovs_vport_cmd_fill_info(struct vport *vport, struct sk_buff *skb,
				   u32 pid, u32 seq, u32 flags, u8 cmd)
{
	struct ovs_header *ovs_header;
	struct ovs_vport_stats vport_stats;
	int err;

	ovs_header = genlmsg_put(skb, pid, seq, &dp_vport_genl_family,
				 flags, cmd);
	if (!ovs_header)
		return -EMSGSIZE;

	ovs_header->dp_ifindex = get_dpifindex(vport->dp);

	if (nla_put_u32(skb, OVS_VPORT_ATTR_PORT_NO, vport->port_no) ||
	    nla_put_u32(skb, OVS_VPORT_ATTR_TYPE, vport->ops->type) ||
	    nla_put_string(skb, OVS_VPORT_ATTR_NAME, vport->ops->get_name(vport)) ||
	    nla_put_u32(skb, OVS_VPORT_ATTR_UPCALL_PID, vport->upcall_pid))
		goto nla_put_failure;

	ovs_vport_get_stats(vport, &vport_stats);
	if (nla_put(skb, OVS_VPORT_ATTR_STATS, sizeof(struct ovs_vport_stats),
		    &vport_stats))
		goto nla_put_failure;

	if (nla_put(skb, OVS_VPORT_ATTR_ADDRESS, ETH_ALEN,
		    vport->ops->get_addr(vport)))
		goto nla_put_failure;

	err = ovs_vport_get_options(vport, skb);
	if (err == -EMSGSIZE)
		goto error;

	return genlmsg_end(skb, ovs_header);

nla_put_failure:
	err = -EMSGSIZE;
error:
	genlmsg_cancel(skb, ovs_header);
	return err;
}

/* Called with RTNL lock or RCU read lock. */
struct sk_buff *ovs_vport_cmd_build_info(struct vport *vport, u32 pid,
					 u32 seq, u8 cmd)
{
	struct sk_buff *skb;
	int retval;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	retval = ovs_vport_cmd_fill_info(vport, skb, pid, seq, 0, cmd);
	if (retval < 0) {
		kfree_skb(skb);
		return ERR_PTR(retval);
	}
	return skb;
}

static int ovs_vport_cmd_validate(struct nlattr *a[OVS_VPORT_ATTR_MAX + 1])
{
	return CHECK_NUL_STRING(a[OVS_VPORT_ATTR_NAME], IFNAMSIZ - 1);
}

/* Called with RTNL lock or RCU read lock. */
static struct vport *lookup_vport(struct net *net,
				  struct ovs_header *ovs_header,
				  struct nlattr *a[OVS_VPORT_ATTR_MAX + 1])
{
	struct datapath *dp;
	struct vport *vport;

	if (a[OVS_VPORT_ATTR_NAME]) {
		vport = ovs_vport_locate(net, nla_data(a[OVS_VPORT_ATTR_NAME]));
		if (!vport)
			return ERR_PTR(-ENODEV);
		if (ovs_header->dp_ifindex &&
		    ovs_header->dp_ifindex != get_dpifindex(vport->dp))
			return ERR_PTR(-ENODEV);
		return vport;
	} else if (a[OVS_VPORT_ATTR_PORT_NO]) {
		u32 port_no = nla_get_u32(a[OVS_VPORT_ATTR_PORT_NO]);

		if (port_no >= DP_MAX_PORTS)
			return ERR_PTR(-EFBIG);

		dp = get_dp(net, ovs_header->dp_ifindex);
		if (!dp)
			return ERR_PTR(-ENODEV);

		vport = ovs_vport_rtnl_rcu(dp, port_no);
		if (!vport)
			return ERR_PTR(-ENOENT);
		return vport;
	} else
		return ERR_PTR(-EINVAL);
}

/* Called with RTNL lock. */
static int change_vport(struct vport *vport,
			struct nlattr *a[OVS_VPORT_ATTR_MAX + 1])
{
	int err = 0;

	if (a[OVS_VPORT_ATTR_STATS])
		ovs_vport_set_stats(vport, nla_data(a[OVS_VPORT_ATTR_STATS]));

	if (a[OVS_VPORT_ATTR_ADDRESS])
		err = ovs_vport_set_addr(vport, nla_data(a[OVS_VPORT_ATTR_ADDRESS]));

	return err;
}

static int ovs_vport_cmd_new(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **a = info->attrs;
	struct ovs_header *ovs_header = info->userhdr;
	struct vport_parms parms;
	struct sk_buff *reply;
	struct vport *vport;
	struct datapath *dp;
	u32 port_no;
	int err;

	err = -EINVAL;
	if (!a[OVS_VPORT_ATTR_NAME] || !a[OVS_VPORT_ATTR_TYPE] ||
	    !a[OVS_VPORT_ATTR_UPCALL_PID])
		goto exit;

	err = ovs_vport_cmd_validate(a);
	if (err)
		goto exit;

	rtnl_lock();
	dp = get_dp(sock_net(skb->sk), ovs_header->dp_ifindex);
	err = -ENODEV;
	if (!dp)
		goto exit_unlock;

	if (a[OVS_VPORT_ATTR_PORT_NO]) {
		port_no = nla_get_u32(a[OVS_VPORT_ATTR_PORT_NO]);

		err = -EFBIG;
		if (port_no >= DP_MAX_PORTS)
			goto exit_unlock;

		vport = ovs_vport_rtnl(dp, port_no);
		err = -EBUSY;
		if (vport)
			goto exit_unlock;
	} else {
		for (port_no = 1; ; port_no++) {
			if (port_no >= DP_MAX_PORTS) {
				err = -EFBIG;
				goto exit_unlock;
			}
			vport = ovs_vport_rtnl(dp, port_no);
			if (!vport)
				break;
		}
	}

	parms.name = nla_data(a[OVS_VPORT_ATTR_NAME]);
	parms.type = nla_get_u32(a[OVS_VPORT_ATTR_TYPE]);
	parms.options = a[OVS_VPORT_ATTR_OPTIONS];
	parms.dp = dp;
	parms.port_no = port_no;
	parms.upcall_pid = nla_get_u32(a[OVS_VPORT_ATTR_UPCALL_PID]);

	vport = new_vport(&parms);
	err = PTR_ERR(vport);
	if (IS_ERR(vport))
		goto exit_unlock;

	ovs_dp_sysfs_add_if(vport);

	err = change_vport(vport, a);
	if (!err) {
		reply = ovs_vport_cmd_build_info(vport, info->snd_pid,
						 info->snd_seq,
						 OVS_VPORT_CMD_NEW);
		if (IS_ERR(reply))
			err = PTR_ERR(reply);
	}
	if (err) {
		ovs_dp_detach_port(vport);
		goto exit_unlock;
	}
	genl_notify(reply, genl_info_net(info), info->snd_pid,
		    ovs_dp_vport_multicast_group.id, info->nlhdr, GFP_KERNEL);

exit_unlock:
	rtnl_unlock();
exit:
	return err;
}

static int ovs_vport_cmd_set(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **a = info->attrs;
	struct sk_buff *reply;
	struct vport *vport;
	int err;

	err = ovs_vport_cmd_validate(a);
	if (err)
		goto exit;

	rtnl_lock();
	vport = lookup_vport(sock_net(skb->sk), info->userhdr, a);
	err = PTR_ERR(vport);
	if (IS_ERR(vport))
		goto exit_unlock;

	err = 0;
	if (a[OVS_VPORT_ATTR_TYPE] &&
	    nla_get_u32(a[OVS_VPORT_ATTR_TYPE]) != vport->ops->type)
		err = -EINVAL;

	if (!err && a[OVS_VPORT_ATTR_OPTIONS])
		err = ovs_vport_set_options(vport, a[OVS_VPORT_ATTR_OPTIONS]);
	if (!err)
		err = change_vport(vport, a);
	else
		goto exit_unlock;
	if (!err && a[OVS_VPORT_ATTR_UPCALL_PID])
		vport->upcall_pid = nla_get_u32(a[OVS_VPORT_ATTR_UPCALL_PID]);

	reply = ovs_vport_cmd_build_info(vport, info->snd_pid, info->snd_seq,
					 OVS_VPORT_CMD_NEW);
	if (IS_ERR(reply)) {
		netlink_set_err(GENL_SOCK(sock_net(skb->sk)), 0,
				ovs_dp_vport_multicast_group.id, PTR_ERR(reply));
		goto exit_unlock;
	}

	genl_notify(reply, genl_info_net(info), info->snd_pid,
		    ovs_dp_vport_multicast_group.id, info->nlhdr, GFP_KERNEL);

exit_unlock:
	rtnl_unlock();
exit:
	return err;
}

static int ovs_vport_cmd_del(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **a = info->attrs;
	struct sk_buff *reply;
	struct vport *vport;
	int err;

	err = ovs_vport_cmd_validate(a);
	if (err)
		goto exit;

	rtnl_lock();
	vport = lookup_vport(sock_net(skb->sk), info->userhdr, a);
	err = PTR_ERR(vport);
	if (IS_ERR(vport))
		goto exit_unlock;

	if (vport->port_no == OVSP_LOCAL) {
		err = -EINVAL;
		goto exit_unlock;
	}

	reply = ovs_vport_cmd_build_info(vport, info->snd_pid, info->snd_seq,
					 OVS_VPORT_CMD_DEL);
	err = PTR_ERR(reply);
	if (IS_ERR(reply))
		goto exit_unlock;

	ovs_dp_detach_port(vport);

	genl_notify(reply, genl_info_net(info), info->snd_pid,
		    ovs_dp_vport_multicast_group.id, info->nlhdr, GFP_KERNEL);

exit_unlock:
	rtnl_unlock();
exit:
	return err;
}

static int ovs_vport_cmd_get(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **a = info->attrs;
	struct ovs_header *ovs_header = info->userhdr;
	struct sk_buff *reply;
	struct vport *vport;
	int err;

	err = ovs_vport_cmd_validate(a);
	if (err)
		goto exit;

	rcu_read_lock();
	vport = lookup_vport(sock_net(skb->sk), ovs_header, a);
	err = PTR_ERR(vport);
	if (IS_ERR(vport))
		goto exit_unlock;

	reply = ovs_vport_cmd_build_info(vport, info->snd_pid, info->snd_seq,
					 OVS_VPORT_CMD_NEW);
	err = PTR_ERR(reply);
	if (IS_ERR(reply))
		goto exit_unlock;

	rcu_read_unlock();

	return genlmsg_reply(reply, info);

exit_unlock:
	rcu_read_unlock();
exit:
	return err;
}

static int ovs_vport_cmd_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct ovs_header *ovs_header = genlmsg_data(nlmsg_data(cb->nlh));
	struct datapath *dp;
	int bucket = cb->args[0], skip = cb->args[1];
	int i, j = 0;

	dp = get_dp(sock_net(skb->sk), ovs_header->dp_ifindex);
	if (!dp)
		return -ENODEV;

	rcu_read_lock();
	for (i = bucket; i < DP_VPORT_HASH_BUCKETS; i++) {
		struct vport *vport;
		struct hlist_node *n;

		j = 0;
		hlist_for_each_entry_rcu(vport, n, &dp->ports[i], dp_hash_node) {
			if (j >= skip &&
			    ovs_vport_cmd_fill_info(vport, skb,
						    NETLINK_CB(cb->skb).pid,
						    cb->nlh->nlmsg_seq,
						    NLM_F_MULTI,
						    OVS_VPORT_CMD_NEW) < 0)
				goto out;

			j++;
		}
		skip = 0;
	}
out:
	rcu_read_unlock();

	cb->args[0] = i;
	cb->args[1] = j;

	return skb->len;
}

static struct genl_ops dp_vport_genl_ops[] = {
	{ .cmd = OVS_VPORT_CMD_NEW,
	  .flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN privilege. */
	  .policy = vport_policy,
	  .doit = ovs_vport_cmd_new
	},
	{ .cmd = OVS_VPORT_CMD_DEL,
	  .flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN privilege. */
	  .policy = vport_policy,
	  .doit = ovs_vport_cmd_del
	},
	{ .cmd = OVS_VPORT_CMD_GET,
	  .flags = 0,		    /* OK for unprivileged users. */
	  .policy = vport_policy,
	  .doit = ovs_vport_cmd_get,
	  .dumpit = ovs_vport_cmd_dump
	},
	{ .cmd = OVS_VPORT_CMD_SET,
	  .flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN privilege. */
	  .policy = vport_policy,
	  .doit = ovs_vport_cmd_set,
	},
};

struct genl_family_and_ops {
	struct genl_family *family;
	struct genl_ops *ops;
	int n_ops;
	struct genl_multicast_group *group;
};

static const struct genl_family_and_ops dp_genl_families[] = {
	{ &dp_datapath_genl_family,
	  dp_datapath_genl_ops, ARRAY_SIZE(dp_datapath_genl_ops),
	  &ovs_dp_datapath_multicast_group },
	{ &dp_vport_genl_family,
	  dp_vport_genl_ops, ARRAY_SIZE(dp_vport_genl_ops),
	  &ovs_dp_vport_multicast_group },
	{ &dp_flow_genl_family,
	  dp_flow_genl_ops, ARRAY_SIZE(dp_flow_genl_ops),
	  &ovs_dp_flow_multicast_group },
	{ &dp_packet_genl_family,
	  dp_packet_genl_ops, ARRAY_SIZE(dp_packet_genl_ops),
	  NULL },
#ifdef LC_ENABLE
	{ &dp_bf_gdt_genl_family,
	  dp_bf_gdt_genl_ops, ARRAY_SIZE(dp_bf_gdt_genl_ops),
	  NULL },
#endif
};

static void dp_unregister_genl(int n_families)
{
	int i;

	for (i = 0; i < n_families; i++)
		genl_unregister_family(dp_genl_families[i].family);
}

static int dp_register_genl(void)
{
	int n_registered;
	int err;
	int i;

	n_registered = 0;
	for (i = 0; i < ARRAY_SIZE(dp_genl_families); i++) {
		const struct genl_family_and_ops *f = &dp_genl_families[i];

		err = genl_register_family_with_ops(f->family, f->ops,
						    f->n_ops);
		if (err)
			goto error;
		n_registered++;

		if (f->group) {
			err = genl_register_mc_group(f->family, f->group);
			if (err)
				goto error;
		}
	}

	return 0;

error:
	dp_unregister_genl(n_registered);
	return err;
}

static int __rehash_flow_table(void *dummy)
{
	struct datapath *dp;
	struct net *net;

	rtnl_lock();
	for_each_net(net) {
		struct ovs_net *ovs_net = net_generic(net, ovs_net_id);

		list_for_each_entry(dp, &ovs_net->dps, list_node) {
			struct flow_table *old_table = genl_dereference(dp->table);
			struct flow_table *new_table;

			new_table = ovs_flow_tbl_rehash(old_table);
			if (!IS_ERR(new_table)) {
				rcu_assign_pointer(dp->table, new_table);
				ovs_flow_tbl_deferred_destroy(old_table);
			}
		}
	}
	rtnl_unlock();
	return 0;
}

static void rehash_flow_table(struct work_struct *work)
{
	genl_exec(__rehash_flow_table, NULL);
	schedule_delayed_work(&rehash_flow_wq, REHASH_FLOW_INTERVAL);
}

static int dp_destroy_all(void *data)
{
	struct datapath *dp, *dp_next;
	struct ovs_net *ovs_net = data;

	list_for_each_entry_safe(dp, dp_next, &ovs_net->dps, list_node)
		__dp_destroy(dp);

	return 0;
}

static int __net_init ovs_init_net(struct net *net)
{
	struct ovs_net *ovs_net = net_generic(net, ovs_net_id);

	INIT_LIST_HEAD(&ovs_net->dps);
	return 0;
}

static void __net_exit ovs_exit_net(struct net *net)
{
	struct ovs_net *ovs_net = net_generic(net, ovs_net_id);

	genl_exec(dp_destroy_all, ovs_net);
}

static struct pernet_operations ovs_net_ops = {
	.init = ovs_init_net,
	.exit = ovs_exit_net,
	.id   = &ovs_net_id,
	.size = sizeof(struct ovs_net),
};

static int __init dp_init(void)
{
	struct sk_buff *dummy_skb;
	int err;

	BUILD_BUG_ON(sizeof(struct ovs_skb_cb) > sizeof(dummy_skb->cb));

	pr_info("Open vSwitch switching datapath %s, built "__DATE__" "__TIME__"\n",
		VERSION);

	err = genl_exec_init();
	if (err)
		goto error;

	err = ovs_workqueues_init();
	if (err)
		goto error_genl_exec;

	err = ovs_tnl_init();
	if (err)
		goto error_wq;

	err = ovs_flow_init();
	if (err)
		goto error_tnl_exit;

	err = ovs_vport_init();
	if (err)
		goto error_flow_exit;

	err = register_pernet_device(&ovs_net_ops);
	if (err)
		goto error_vport_exit;

	err = register_netdevice_notifier(&ovs_dp_device_notifier);
	if (err)
		goto error_netns_exit;

	err = dp_register_genl();
	if (err < 0)
		goto error_unreg_notifier;

	schedule_delayed_work(&rehash_flow_wq, REHASH_FLOW_INTERVAL);

	return 0;

error_unreg_notifier:
	unregister_netdevice_notifier(&ovs_dp_device_notifier);
error_netns_exit:
	unregister_pernet_device(&ovs_net_ops);
error_vport_exit:
	ovs_vport_exit();
error_flow_exit:
	ovs_flow_exit();
error_tnl_exit:
	ovs_tnl_exit();
error_wq:
	ovs_workqueues_exit();
error_genl_exec:
	genl_exec_exit();
error:
	return err;
}

static void dp_cleanup(void)
{
	cancel_delayed_work_sync(&rehash_flow_wq);
	dp_unregister_genl(ARRAY_SIZE(dp_genl_families));
	unregister_netdevice_notifier(&ovs_dp_device_notifier);
	unregister_pernet_device(&ovs_net_ops);
	rcu_barrier();
	ovs_vport_exit();
	ovs_flow_exit();
	ovs_tnl_exit();
	ovs_workqueues_exit();
	genl_exec_exit();
}

module_init(dp_init);
module_exit(dp_cleanup);

MODULE_DESCRIPTION("Open vSwitch switching datapath");
MODULE_LICENSE("GPL");
