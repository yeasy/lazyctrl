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
#endif

#include "bf.h"

#define SETBIT(array, i) (array[i/SIZE_CHAR] |= (1<<(i%SIZE_CHAR)))
#define GETBIT(array, i) (array[i/SIZE_CHAR] & (1<<(i%SIZE_CHAR)))

/**
 * hash function: SAX.
 */
u32 sax_hash(const unsigned char *key)
{
    u32 h=0;
    while(*key) h^=(h<<5)+(h>>2)+(unsigned char)*key++;
    return h;
}

/**
 * hash function: sdbm.
 */
u32 sdbm_hash(const unsigned char *key)
{
    u32 h=0;
    while(*key) h=(unsigned char)*key++ + (h<<6) + (h<<16) - h;
    return h;
}

hashfunc_t hashFunctions[] = {sax_hash,sdbm_hash};

/**
 * Create array bloom_filter.
 * @param bf_id: bf_id for the bloom_filter
 * @param len: number of the bits in one bloom_filter
 * @param port_no: port_no associated.
 * @param nfuncs: number of the hash functions
 * @return: Pointer to the created bloom_filter. NULL if failed.
 */
struct bloom_filter *bf_create(u32 bf_id, u32 len, u16 port_no, u32 nfuncs)
{
    struct bloom_filter *bf;
    int i;
    
#ifdef __KERNEL__
    if(!(bf=kmalloc(sizeof(struct bloom_filter),GFP_KERNEL))) {
#else
    if(!(bf=malloc(sizeof(struct bloom_filter)))) {
#endif
        /*printk("Error in kmalloc bf\i");*/
        return NULL;
    }
/*
#ifdef __KERNEL__
    if(!(bf->array=kcalloc((len+SIZE_CHAR-1)/SIZE_CHAR, SIZE_CHAR,GFP_KERNEL))) {
        printk("Error in kcalloc bf\i");
        kfree(bf);
#else
    if(!(bf->array=calloc((len+SIZE_CHAR-1)/SIZE_CHAR, SIZE_CHAR))) {
        free(bf);
#endif
        return NULL;
    }
*/

#ifdef __KERNEL__
    if(!(bf->funcs=(hashfunc_t*)kmalloc(nfuncs*sizeof(hashfunc_t),GFP_KERNEL))) {
        kfree(bf->array);
        kfree(bf);
#else
    if(!(bf->funcs=(hashfunc_t*)malloc(nfuncs*sizeof(hashfunc_t)))) {
        free(bf->array);
        free(bf);
#endif
        /*printk("Error in kmalloc hashfuncs\i");*/
        return NULL;
    }

    for(i=0; i<nfuncs; ++i) {
        bf->funcs[i]=hashFunctions[i];
    }
    bf->nfuncs=nfuncs;

    memset(bf->array,0,len/SIZE_CHAR);
    bf->bf_id = bf_id;
    bf->port_no = port_no;
    bf->len=len;

    return bf;
}

/**
 * destroy a bloom_filter.
 * @param bf: the bloom_filter to destroy
 */
int bf_destroy(struct bloom_filter *bf)
{
#ifdef __KERNEL__
    kfree(bf->array);
    kfree(bf->funcs);
    kfree(bf);
#else
    free(bf->array);
    free(bf->funcs);
    free(bf);
#endif
    return 0;
}

/**
 * add a new string to bf
 * @param bf: the bloom_filter to update
 * @param s: the string to add
 */
int bf_add(struct bloom_filter *bf, const unsigned char *s)
{
    if (!bf)
        return -1;

    u32 i;
    for(i=0; i<bf->nfuncs; ++i) {
        SETBIT(bf->array, bf->funcs[i](s)%bf->len);
    }

#ifdef DEBUG
#ifndef __KERNEL__
    VLOG_INFO("Add %s into bf with id=0x%x,nfunc=%u",s,bf->bf_id,bf->nfuncs);
#endif
#endif
    return 0;
}

/**
 * test if s is in bf.
 * @param bf: the bloom_filter to test
 * @param s: the string to test
 * @return 1 if true
 */
int bf_check(struct bloom_filter *bf, unsigned char *s)
{
    u32 i;

    if (!bf || !bf->array)
        return 0;
#ifdef DEBUG
#ifdef __KERNEL__
	printk(KERN_INFO "bf->len=%u,id=0x%x,nfunc=%u",bf->len,bf->bf_id,bf->nfuncs);
#endif
#endif

    for(i=0; i<bf->nfuncs; ++i) {
#ifdef DEBUG
#ifdef __KERNEL__
	pr_info("test bit %u=%u",bf->funcs[i](s)%bf->len,GETBIT(bf->array, bf->funcs[i](s)%bf->len));
#endif
#endif
        if(!(GETBIT(bf->array, bf->funcs[i](s)%bf->len))) {
            return 0;
        }
    }

    return 1;
}

/**
 * set the array content.
 * @param bf: bf who owns the array
 * @param array: new array content
 * @param len: new length
 * @return 0 if success, else -1
 */
int bf_set_array(struct bloom_filter *bf, const u8 *array, u32 len)
{
    if (!bf || !array)
        return -1;
    /*
    u8 * tmp_array = NULL;
#ifdef __KERNEL__
    if(!(tmp_array=kcalloc((len+SIZE_CHAR-1)/SIZE_CHAR, SIZE_CHAR,GFP_KERNEL))) {
        kfree(tmp_array);
        printk("Error when kcalloc in bf_set_array()\n");
        return -1;
    }
    kfree(bf->array);
#else
    if(!(tmp_array=calloc((len+SIZE_CHAR-1)/SIZE_CHAR, SIZE_CHAR))) {
        free(tmp_array);
        printk("Error when kcalloc in bf_set_array()\n");
        return -1;
    }
    free(bf->array);
#endif
    memcpy(tmp_array,array,(len+SIZE_CHAR-1)/SIZE_CHAR);
    bf->array = tmp_array;
    */
    memcpy(bf->array,array,(len+SIZE_CHAR-1)/SIZE_CHAR);
    bf->len = len;
    return 0;
}
