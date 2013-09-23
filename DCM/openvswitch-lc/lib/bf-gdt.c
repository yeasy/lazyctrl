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

#ifdef __KERNEL__
#include<linux/slab.h>
#include<linux/module.h>
#include<linux/init.h>
#include<linux/kernel.h>
#else
#include<stdarg.h>
#include<stdlib.h>
#include "vlog.h"
VLOG_DEFINE_THIS_MODULE(bridge);
#endif

#include "bf-gdt.h"

/**
 * Init an empty bf_gdt with no bf yet.
 * @param gid: the group id
 * @return: Pointer to the created bf_gdt. NULL if failed.
 */
struct bf_gdt *bf_gdt_init(u32 gid)
{
    struct bf_gdt *gdt;
#ifdef __KERNEL__
    if(!(gdt=kmalloc(sizeof(struct bf_gdt),GFP_KERNEL))) 
#else
    if(!(gdt=malloc(sizeof(struct bf_gdt)))) 
#endif
    {
#ifdef __KERNEL__
	pr_info(KERN_INFO "bf_gdt_init(): Error in kmalloc gdt");
#endif
        return NULL;
    }
#ifdef __KERNEL__
    if(!(gdt->bf_array=kcalloc(BF_GDT_MAX_FILTERS,sizeof(struct bloom_filter*),GFP_KERNEL))) 
#else
    if(!(gdt->bf_array=calloc(BF_GDT_MAX_FILTERS,sizeof(struct bloom_filter*)))) 
#endif
    {
#ifdef __KERNEL__
	pr_info(KERN_INFO "bf_gdt_init(): Error in kmalloc bf_array");
#endif
        return NULL;
    }

    gdt->gid = gid;
    gdt->nbf = 0;

    return gdt;
}

/**
 * Create and add a new empty bloom_filter into the given gdt
 * @param gdt: the gdt to update
 * @param bf_id: the bf_id
 * @param port_no: the port_no of the new bloom_filter
 * @param len: the bit len of the new bloom_filter, should be LC_BF_DFT_LEN
 * @return: Pointer to the created bloom_filter. NULL if failed.
 */
struct bloom_filter *bf_gdt_add_filter(struct bf_gdt *gdt, u32 bf_id, u16 port_no, u32 len)
{
#ifdef DEBUG
#ifdef __KERNEL__
	pr_info("add_filter(): gdt->nbf=%u",gdt->nbf);
#else
	VLOG_INFO("add_filter(): gdt->nbf=%u",gdt->nbf);
#endif
#endif
    if (!gdt || gdt->nbf >= BF_GDT_MAX_FILTERS) {
        return NULL;
    }

    struct bloom_filter *bf = bf_create(bf_id,len,port_no,2);
    if (bf) {
        gdt->bf_array[gdt->nbf++] = bf;
        return bf;
    }
    return NULL;
}

/**
 * Create and add a new empty bloom_filter into the given gdt
 * @param gdt: the gdt to update
 * @param bf_id: the bf_id
 * @return -1 if failed, 0 if succeed, 1 if not found.
 */
int bf_gdt_del_filter(struct bf_gdt *gdt, u32 bf_id)
{
    if (!gdt) {
        return -1;
    }

    int i = 0;
    for (i=0;i<gdt->nbf;i++){
        if(gdt->bf_array[i]->bf_id == bf_id) {
#ifdef __KERNEL__
            kfree(gdt->bf_array[i]);
#else
            free(gdt->bf_array[i]);
#endif
            if(i != gdt->nbf-1) {//not the last one
                gdt->bf_array[i] = gdt->bf_array[gdt->nbf-1];
            }
            gdt->bf_array[gdt->nbf-1] = NULL;
            gdt->nbf -= 1;
            return 0;
        }
    }
    return 1;
}

/**
 * Insert a bloom_filter content into the given gdt, guarantee no existed yet.
 * the given bloom_filter can be freed outside.
 * @param gdt: the gdt to update
 * @param bf: the bf to insert.
 * @return: Pointer to the inserted bloom_filter. NULL if failed.
 */
struct bloom_filter *bf_gdt_insert_filter(struct bf_gdt *gdt, struct bloom_filter *bf)
{
    if (!gdt || !bf) {
        return NULL;
    }
    struct bloom_filter *new_bf = bf_gdt_add_filter(gdt,bf->bf_id,bf->port_no,bf->len);
    if (new_bf) {
#ifdef DEBUG
#ifdef __KERNEL__
        pr_info("create new_bf successfully,id=0x%x,port_no=%u,len=%u.",bf->bf_id,bf->port_no,bf->len);
#else
        VLOG_INFO("create new_bf successfully,id=0x%x,port_no=%u,len=%u.",bf->bf_id,bf->port_no,bf->len);
#endif
#endif
        memcpy(new_bf->array,bf->array,new_bf->len/SIZE_CHAR);
    }
    return new_bf;
}


/**
 * Find a bf in gdt if existed.
 * @param gdt: the gdt to find with
 * @param bf_id: the bloom_filter's id
 * @return: Pointer to the bloom_filter. NULL if failed.
 */
struct bloom_filter *bf_gdt_find_filter(struct bf_gdt *gdt, u32 bf_id)
{
    if (gdt) {
        int i = 0;
        for (i=0;i<gdt->nbf;i++){
            if(gdt->bf_array[i]->bf_id == bf_id)
                return gdt->bf_array[i];
        }
    }
    return NULL;
}

/**
 * Update a remote bloom_filter into the given gdt
 * @param gdt: the gdt to update
 * @param bf: the new bloom_filter
 * @return: 1 update, 2 add new.
 */
int bf_gdt_update_filter(struct bf_gdt *gdt, struct bloom_filter *bf)
{
    if (!gdt || !bf) {
#ifdef DEBUG
#ifdef __KERNEL__
        pr_info("bf_gdt_update_filter():gdt or bf NULL.");
#else
        VLOG_INFO("bf_gdt_update_filter():gdt or bf NULL.");
#endif
#endif
        return -1;
    }
#ifdef DEBUG
#ifdef __KERNEL__
    pr_info(">>>bf_gdt_update_filter(),nbf=%u",gdt->nbf);
#else
    VLOG_INFO(">>>bf_gdt_update_filter(),nbf=%u",gdt->nbf);
#endif
#endif
    int ret = -1;
    struct bloom_filter *matched_bf = bf_gdt_find_filter(gdt,bf->bf_id);
#ifdef DEBUG
#ifdef __KERNEL__
    pr_info("bf_gdt_update_filter():bf->id=0x%x,len=%u,port_no=%u.",bf->bf_id,bf->len,bf->port_no);
#else
    VLOG_INFO("bf_gdt_update_filter():bf->id=0x%x,len=%u,port_no=%u.",bf->bf_id,bf->len,bf->port_no);
#endif
#endif
    if(matched_bf) {//find matched.
        if(memcmp(matched_bf->array,bf->array,matched_bf->len/SIZE_CHAR)==0) { //no change
#ifdef DEBUG
#ifdef __KERNEL__
            pr_info("bf_gdt_update_filter(): no change.");
#else
            VLOG_INFO("bf_gdt_update_filter(): no change.");
#endif
#endif
            ret = -1;
        } else {//content changed, then update
#ifdef DEBUG
#ifdef __KERNEL__
            pr_info("bf_gdt_update_filter():update existed, bf_id=0x%x, len=%u",bf->bf_id,bf->len);
#else
            VLOG_INFO("bf_gdt_update_filter():update existed, bf_id=0x%x, len=%u",bf->bf_id,bf->len);
#endif
#endif
            memcpy(matched_bf->array,bf->array,matched_bf->len/SIZE_CHAR);
            ret = 1;
        }
    } else { //no existed, then insert
#ifdef DEBUG
#ifdef __KERNEL__
        pr_info("bf_gdt_update_filter():add new, bf_id=0x%x, len=%u",bf->bf_id,bf->len);
#else
        VLOG_INFO("bf_gdt_update_filter():add new, bf_id=0x%x, len=%u",bf->bf_id,bf->len);
#endif
#endif
        bf_gdt_insert_filter(gdt,bf);
        ret = 2;
    }
#ifdef DEBUG
#ifdef __KERNEL__
    pr_info("<<<bf_gdt_update_filter(),nbf=%u",gdt->nbf);
#else
    VLOG_INFO("<<<bf_gdt_update_filter(),nbf=%u",gdt->nbf);
#endif
#endif

    return ret;
}

/**
 * destroy a bf_gdt.
 * @param gdt: the bf_gdt to destroy.
 */
int bf_gdt_destroy(struct bf_gdt *gdt)
{
    if(!gdt) return 0;
    if (gdt->bf_array) {
        u32 i = 0;
        for (i=0;i<gdt->nbf;i++){
            bf_destroy(gdt->bf_array[i]);
        }
    }
#ifdef __KERNEL__
    if (gdt->bf_array)
        kfree(gdt->bf_array);
    if (gdt)
        kfree(gdt);
#else
    if (gdt->bf_array)
        free(gdt->bf_array);
    if (gdt)
        free(gdt);
#endif
    return 0;
}

/**
 * add a new string to gdt's some bf
 * @param gdt: the bf_gdt to update
 * @param bf_id: the dp's content updated
 * @param s: the string to add
 * @return 0 if successfully.
 */
int bf_gdt_add_item(struct bf_gdt *gdt, u32 bf_id, const unsigned char *s)
{
    u32 i=0;
    struct bloom_filter *bf = NULL;
    if (gdt && gdt->bf_array) {
        for(i=0; i<gdt->nbf; ++i) {
            bf = gdt->bf_array[i];
            if(bf->bf_id == bf_id) 
                return bf_add(bf,s);
        }
    }

    return -1;
}

/**
 * test if s is in gdt-gdt.
 * @param gdt: the bf_gdt to test
 * @param s: the string to test
 * @return the bf if true
 */
struct bloom_filter *bf_gdt_check(struct bf_gdt *gdt, unsigned char *s)
{
    u32 i;
#ifdef DEBUG
#ifdef __KERNEL__
    pr_info(KERN_INFO "gdt_check():gdt->nbf=%u",gdt->nbf);
#endif
#endif
    if (gdt && gdt->bf_array) {
        for(i=0; i<gdt->nbf; ++i) {
            if(bf_check(gdt->bf_array[i],s)) return gdt->bf_array[i];
        }
    }

    return NULL;
}
