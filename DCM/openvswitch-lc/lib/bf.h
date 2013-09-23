/*
 * Copyright (c) 2007-2012 IBM CRL.
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

#ifndef BF_H
#define BF_H

/* only for userspace compatibility */
#ifndef __KERNEL__
#include<stdint.h>
typedef uint32_t u32;
typedef unsigned short u16;
typedef unsigned char u8;
#endif

#ifndef SIZE_CHAR
#define SIZE_CHAR 8
#endif

#ifndef LC_BF_DFT_LEN
#define LC_BF_DFT_LEN 1024
#endif

#define LC_BF_LOCAL_ID 0xc0a8390a //ip of local edge sw: 192.168.57.10
#define LC_BF_LOCAL_PORT 0xfffe //local port OFPP_LOCAL
#define LC_BF_REMOTE_PORT 1 //remote port

typedef u32 (*hashfunc_t)(const unsigned char *);

struct bloom_filter{
    u32 bf_id; /*id, should be the id of the switch, or the ip of the dp.*/
    u32 len; /*bit length of the bit array*/
    u16 port_no; /*the port send out, should be LC_BF_REMOTE_PORT.*/
    u8 array[128]; /*the bit array, defaultly LC_BF_DFT_LEN bit*/
    u32 nfuncs; /*number of hash functions*/
    hashfunc_t *funcs; /*hash functions array*/
};

u32 sax_hash(const unsigned char *key);
u32 sdbm_hash(const unsigned char *key);

struct bloom_filter *bf_create(u32 bf_id, u32 len, u16 port_no, u32 nfuncs); 
int bf_destroy(struct bloom_filter *bf);
int bf_add(struct bloom_filter *bf, const unsigned char *s);
int bf_check(struct bloom_filter *bf, unsigned char *s);
int bf_set_array(struct bloom_filter *bf, const u8 *array, u32 len);

#endif
