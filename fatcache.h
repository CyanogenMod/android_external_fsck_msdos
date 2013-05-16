/*
*Copyright (c) 2012, The Linux Foundation. All rights reserved.
*Redistribution and use in source and binary forms, with or without
*modification, are permitted provided that the following conditions are
*met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

*THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
*WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
*MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
*ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
*BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
*CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
*SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
*BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
*WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
*OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
*IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
*/

#ifndef _FATCACHE_H_
#define	_FATCACHE_H_
#include "dosfs.h"
#include "tree.h"
#include "stddef.h"
#include <cutils/log.h>
#include <android/log.h>
#define EMPTY_FAT	(( struct cluster_chain_descriptor*)0)
#define	EMPTY_CACHE	(( struct fatcache*)0)
#define	BIT(x,n)	(((x)>>(n)) & 0x1)
#define	SET_BIT(x,n)	do{	\
	x |= 1<<n;}while(0)
#define	CLEAR_BIT(x,n)	do{	\
	x &= ~(1<<n);}while(0)

/*
 *print information when handle cluster chain
 */
#define	FSCK_SLOGI(...)	((void)__android_log_buf_print(LOG_ID_SYSTEM, ANDROID_LOG_INFO,"fsck_msdos", __VA_ARGS__))
#define	FSCK_SLOGW(...)	((void)__android_log_buf_print(LOG_ID_SYSTEM, ANDROID_LOG_WARN,"fsck_msdos", __VA_ARGS__))
#define	FSCK_SLOGE(...)	((void)__android_log_buf_print(LOG_ID_SYSTEM, ANDROID_LOG_ERROR,"fsck_msdos", __VA_ARGS__))
#define	FSCK_SLOGD(...)	((void)__android_log_buf_print(LOG_ID_SYSTEM, ANDROID_LOG_DEBUG,"fsck_msdos", __VA_ARGS__))
#define	fsck_info		FSCK_SLOGI
#define	fsck_warn		FSCK_SLOGW
#define	fsck_err		FSCK_SLOGE
#define	fsck_debug		FSCK_SLOGD
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define container_of(ptr, type, member) \
	((type *)((unsigned long)(ptr) - offsetof(type, member)))
int fsck_msdos_cache_compare(struct cluster_chain_descriptor *fat1,struct cluster_chain_descriptor *fat2);
RB_HEAD(FSCK_MSDOS_CACHE,cluster_chain_descriptor);
extern RB_FIND(name, x, y);
extern RB_REMOVE(name, x, y);
extern RB_NEXT(name, x, y);
extern RB_INSERT(name, x, y);
extern struct FSCK_MSDOS_CACHE rb_root;
extern unsigned int * fat_bitmap;
typedef unsigned char u_char;
/*if necessary ,we can find the nextclust from FAT table*/
unsigned int  GetNextClusFromFAT(struct bootblock *boot,u_char*fatable,unsigned int  clust);
/*set the next cluster in FAT table*/
void SetNextClusToFAT(struct bootblock*boot,u_char*fat ,unsigned int cl ,unsigned int  next);
struct cluster_chain_descriptor* New_fatentry(void);
struct fatcache* New_fatcache(void);
/*insert an new fatcache to fatentry . if exist ,merge it*/
int add_fatcache_To_ClusterChain(struct cluster_chain_descriptor *fatentry ,struct fatcache *new);
/*add an new fatcache to the tail of fatentry*/
int add_fatcache_Totail(struct cluster_chain_descriptor *fatentry ,struct fatcache *new);
/*find the cache which the cl is belong to ,cache2 return the prev fatcache*/
struct fatcache *Find_cache(struct cluster_chain_descriptor *fat,unsigned int cl,struct fatcache**cache2);
/*find the next cluster*/
struct fatcache	*Find_nextclus(struct cluster_chain_descriptor* fat,unsigned int clus, unsigned int* cl);
int delete_fatcache_below(struct cluster_chain_descriptor* fatentry,struct fatcache*cache);
void Trunc(struct bootblock *boot, struct cluster_chain_descriptor *fat, unsigned int cl);
void free_rb_tree(void);
/*for test*/
void Dump_fatentry(struct cluster_chain_descriptor *fat);
#endif
