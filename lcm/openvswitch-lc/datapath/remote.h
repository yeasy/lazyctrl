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

#ifndef REMOTE_H
#define REMOTE_H 1

#include <linux/if_vlan.h>
#include <linux/skbuff.h>
#include <linux/version.h>
#include "datapath.h"

int __remote_encapulation(struct datapath *dp, struct sk_buff *skb, int dst_ip);
int __remote_decapulation(struct sk_buff *skb);

#endif /* remote.h */
