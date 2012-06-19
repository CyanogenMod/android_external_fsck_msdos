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
#include <string.h>
#include "dosfs.h"
#include "ext.h"
#include "fatcache.h"
#include "fsutil.h"
#include <stdio.h>
#include <unistd.h>
#include "tree.h"
int fsck_msdos_cache_compare(struct cluster_chain_descriptor *fat1,struct cluster_chain_descriptor *fat2)
{
	if(fat1->head > fat2->head)
		return 1;
	else if(fat1->head < fat2->head)
		return -1;
	else
		return 0;
}
struct FSCK_MSDOS_CACHE rb_root;
RB_GENERATE(FSCK_MSDOS_CACHE,cluster_chain_descriptor,rb,fsck_msdos_cache_compare);
unsigned int GetNextClusFromFAT( struct bootblock*boot,u_char*fatable,unsigned int clust)
{
	unsigned int nextclus;
	if(!fatable){
		fsck_err("%s:No FAT table \n",__func__);
		return 0;
	}
	switch(boot->ClustMask){
		case CLUST32_MASK:
			nextclus = fatable[4*clust] + (fatable[4*clust+1]<<8) + (fatable[4*clust+2]<<16) + (fatable[4*clust+3]<<24);
			nextclus &= boot->ClustMask;
			break;

		case CLUST16_MASK:
			nextclus = fatable[2*clust] + (fatable[2*clust+1]<<8);
			nextclus &= boot->ClustMask;
			break;

		default:
			if(clust & 0x1)
				nextclus = ((fatable[3*(clust/2)+1]>>4) + (fatable[3*(clust/2)+2]<<4)) & 0x0fff;
			else
				nextclus = (fatable[3*(clust/2)] + (fatable[3*(clust/2)+1]<<8)) & 0x0fff;
			break;
	}
	return nextclus;
}

void SetNextClusToFAT(struct bootblock*boot,u_char*fat ,unsigned int cl ,unsigned int next)
{
	/*fat must point to the head of FAT*/
	u_char* p;
	if(!fat){
		fsck_err("%s :No FAT table \n",__func__);
		return ;
	}
	switch(boot->ClustMask){
		case CLUST32_MASK:
			p = fat + 4*cl;
			*p++ = (u_char)next;
			*p++ = (u_char)(next >>8);
			*p++ = (u_char)(next >> 16);
			*p &= 0xf0;
			*p = (next >> 24) & 0x0f;
			break;

		case CLUST16_MASK:
			p = fat + 2*cl;
			*p++ = (u_char)next;
			*p = (u_char)(next >> 8);
			break;

		default:
			p = fat + 3*(cl/2);
			if(cl & 0x1){
				p++;
				*p++ |= (u_char)(next << 4);
				*p = (u_char)(next >> 4);
			}else{
				*p++ = (u_char)next;
				*p |= (next >> 8) & 0x0f;
			}
			break;

	}
}

/*
 *dump a cluster chain for test
 */
void Dump_fatentry(struct cluster_chain_descriptor *fat)
{
	struct fatcache *cache;
	if(!fat){
		fsck_warn("%s;NULL  pointer\n",__func__);
		return ;
	}
	fsck_info("head: 0x%d:",fat->head);
	cache = fat->child;
	while(cache){
		fsck_info(" 0x%d:0x%d ->" ,cache->head,cache->length);
		cache = cache->next;
	}
	fsck_info("EOF\n");
}

int add_fatcache_To_ClusterChain(struct cluster_chain_descriptor *fatentry ,struct fatcache *new)
{
	struct fatcache *cache = fatentry->child;
	if(!fatentry || !new){
		fsck_warn("%s:NULL pointer\n",__func__);
		return -1;
	}
	/*NULL*/
	if(!cache){
		fatentry->child = new;
		new->next = NULL;
		fatentry->head = new->head;
		fatentry->length = new->length;
		return 0;
	}
	/*DO NOT sort,just add to the tail*/
	while(cache->next){
		cache = cache->next;
	}
	cache->next = new;
	new->next = NULL;
	fatentry->length += new->length;
	return 0;
}
/*
 *Function : add_fatcache_Totail
 *this function is used to merge two existing cluster chain
 *It just add a fatcache to the tail of another fatentry
 */
int add_fatcache_Totail(struct cluster_chain_descriptor *fatentry ,struct fatcache *new)
{
	struct fatcache *cache;
	if(!fatentry || !new || !fatentry->child){
		fsck_warn("%s:NULL pointer\n",__func__);
		return -1;
	}
	cache = fatentry->child;
	while(cache->next){
		cache = cache->next;
	}
	cache->next = new;
	return 0;
}
/*
 *NOTE:
 *if *prev_cache = return cache,that means the cache we find in cluster chain is the first one
 */
struct fatcache *Find_cache(struct cluster_chain_descriptor *fat,unsigned int cl ,struct fatcache**prev_cache)
{
	struct fatcache *cache = fat->child,*prev;
	prev = cache;
	while(cache){
		if( cl >= cache->head && cl < (cache->head + cache->length)){
			*prev_cache = prev;
			return cache;
		}
		prev = cache;
		cache = cache->next;
	}
	return EMPTY_CACHE;
}

/*
*find the next cluster from fatentry
*/
struct fatcache* Find_nextclus(struct cluster_chain_descriptor* fat,unsigned int clus, unsigned int* cl)
{
	struct fatcache* cache = fat->child;
	*cl = 0x0;
	if(!cache){
		fsck_warn("Not find the cluster after cluster %d\n",clus);
		return (struct fatcache*)0;
	}
	if(clus < fat->head){
		fsck_warn("out of range,clus: %d ,fat->head:%d\n",clus,fat->head);
		return (struct fatcache*)0;
	}
	while(cache){
		if(clus >= cache->head && clus <= cache->head + cache->length -2 ){
			*cl =  clus + 1;
			return cache;
		}
		if(clus == cache->head + cache->length -1 ){
			cache = cache->next;
			if(cache){
				*cl = cache->head;
				return cache;
			}else{
				*cl = CLUST_EOF;
				return (struct fatcache*)0;
			}
		}
		cache = cache->next;
	}
	return EMPTY_CACHE;
}
int delete_fatcache_below(struct cluster_chain_descriptor * fatentry,struct fatcache*cache)
{
	struct fatcache *curr = cache,*next,*last;
	last = cache;
	if(!cache || !fatentry){
		fsck_warn("%s:NULL pointer\n",__func__);
		return -1;
	}
	next = curr->next;
	if(!next)
		return 0;
	while(next){
		curr = next;
		next = next->next;
		fatentry->length -= curr->length;
		free((void*)curr);
	}
	last->next = NULL;
	return 0;
}

/*remove clusters after cl*/
void Trunc(struct cluster_chain_descriptor *fat, unsigned int cl)
{
	fsck_info("fat :%p ,cl : %d \n",fat,cl);
	struct fatcache*prev ;
	struct fatcache*cache =	Find_cache(fat,cl,&prev);
	if(!cache)
		return ;
	delete_fatcache_below(fat,cache);
	cache->length = cl - cache->head + 1;
	fat->length -= (cache->length - (cl - cache->head) - 1);
}

struct cluster_chain_descriptor* New_fatentry(void)
{
		struct cluster_chain_descriptor *fat;
		fat = calloc(1,sizeof(struct cluster_chain_descriptor));
		if(!fat){
			fsck_warn("No space\n");
			return fat;
		}
		RB_SET(fat, NULL, rb);
		return fat;
}

struct fatcache* New_fatcache(void)
{
		struct fatcache *cache;
		cache = calloc(1,sizeof(struct fatcache));
		if(!cache)
			fsck_warn("No space \n");
		return cache;
}

void free_rb_tree(void)
{
	struct cluster_chain_descriptor *fat ,*next_fat;
	struct fatcache *cache ,*next_cache;
	fsck_info("%s \n",__func__);
	fat = RB_MIN(FSCK_MSDOS_CACHE,&rb_root);
	if(!fat){
		fsck_info("%s :rb tree is empty\n",__func__);
		return ;
	}
	while(fat){
		cache = fat->child;
		if(!cache)
			continue;
		while(cache->next){
			next_cache = cache->next;
			free(cache);
			cache = next_cache;
		}
		next_fat = RB_NEXT(FSCK_MSDOS_CACHE,0,fat);
		/*must remove from rb tree before free*/
		RB_REMOVE(FSCK_MSDOS_CACHE,&rb_root,fat);
		free(fat);
		fat = next_fat;
	}
}
