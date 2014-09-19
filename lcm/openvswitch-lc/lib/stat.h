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

#ifndef STAT_H
#define STAT_H

/**
 * TODO: should collect stat in sw pairs.
 * currently, only transfer 1 entry.
 */
#define LC_NUM_STAT_ENTRY 1

/**
 * store the stat information for one sw-sw pair.
 */
struct stat_entry {
    unsigned int src_sw_id;
    unsigned int dst_sw_id;
    unsigned long bytes;
};

struct stat_base {
    unsigned int num; //number of entries.
    struct stat_entry entry[LC_NUM_STAT_ENTRY]; //pointer to entries.
    unsigned int cpu;
};
#endif
