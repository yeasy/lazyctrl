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
 *
 * TODO: the timeout scheme for bf entries. This could be implemented
 * with countable-bloom_filter data structure.
 */

#ifndef BF_GDT_H
#define BF_GDT_H

#include "bf.h"

#ifndef BF_GDT_MAX_FILTERS
#define BF_GDT_MAX_FILTERS 64
#endif

#ifndef LC_GROUP_DFT_ID
#define LC_GROUP_DFT_ID 0
#endif

struct bf_gdt{
    u32 gid; /*id of the group, dp should not care*/
    u32 nbf; /*number of the bloom_filters*/
    struct bloom_filter **bf_array; /*the bloom_filter array*/
};

struct bf_gdt *bf_gdt_init(u32 gid);
int bf_gdt_destroy(struct bf_gdt *gdt);

struct bloom_filter *bf_gdt_add_filter(struct bf_gdt *gdt, u32 bf_id, u16 port_no, u32 len);
int bf_gdt_del_filter(struct bf_gdt *gdt, u32 bf_id);
int bf_gdt_add_item(struct bf_gdt *gdt, u32 bf_id, const unsigned char *s);

struct bloom_filter *bf_gdt_check(struct bf_gdt *gdt, unsigned char *s);

struct bloom_filter *bf_gdt_find_filter(struct bf_gdt *gdt, u32 bf_id);
struct bloom_filter *bf_gdt_insert_filter(struct bf_gdt *gdt, struct bloom_filter *bf);
int bf_gdt_update_filter(struct bf_gdt *gdt, struct bloom_filter *bf);
#endif
