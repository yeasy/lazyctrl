/*
 * Copyright (c) 2012-2015 IBM CRL
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

#ifndef LC_GROUP_H
#define LC_GROUP_H 1

#include <linux/types.h>

#include "../lib/bf.h"

#define LC_GROUP_UNDEF -1

/**
 * struct lc_group 
 * To maintain the dc group orgnization.
 * @id: Group id.
 */
struct lc_group {
    u16 id;
};

int lc_group_init(struct lc_group *group);
void ovs_lc_group_free(struct lc_group *group);

#endif /* lc_group.h */
