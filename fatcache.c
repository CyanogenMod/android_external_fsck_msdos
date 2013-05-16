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
#include "fragment.h"
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

/*
 *Function GetNextClusFromFAT
 *PURPUSE: reconvert cluster fat from FAT table in memory
 *PARAMETERS:
 * boot -> pointer to the boot sector of FAT
 * fatable -> pointer to the FAT table in memory
 * clust ->get the next cluster of paramter clust
 */
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

/*
 *Function SetNextClusToFAT
 *PURPUSE: reconvert FAT table from cluster fats when write modified FAT table to media
 *PARAMETERS:
 *	boot -> pointer to the boot sector of FAT
 *	fat -> pointer to the FAT table in memory
 *	cl ->set the next cluster of paramter cl
 *	next -> the  next cluster of cl
 */
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
			*p |= (next >> 24) & 0x0f;
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
 *Function Dump_fatentry
 *PURPUSE: dump cluster fat information for debug
 *PARAMETERS:
 * fat -> pointer to a cluster fat descripor
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

/*
 *Function add_fatcache_To_Clusterfat
 *PURPUSE: add continuous clusters to cluster fat
 *PARAMETERS:
 * fatentry -> pointer to a cluster fat descripor which this fatcache be added to
 * new -> a new fatcache which represent some continuous clusters
 *NOTE: this function will update length in cluster_fat_descriptor
 * pls compare this with function add_fatcache_Totail
 */
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
 *Function add_fatcache_Totail_WithOutUpdate
 *PURPUSE: add a fatcache to the tail of another cluster_fat_descriptor,be used to merge two existing cluster fat
 *PARAMETERS:
 * fatentry -> pointer to a cluster fat descripor which this fatcache be added to
 * new -> a new fatcache which represent some continuous clusters
 *NOTE: this function will NOT update length in cluster_fat_descriptor
 * pls compare this with function add_fatcache_To_Clusterfat
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
 *Function Find_cache
 *PURPUSE: find a fatcache from cluster_fat_descriptor by cluster number cl
 *PARAMETERS:
 * fat -> pointer to a cluster fat descripor
 * cl -> cluster number
 * prev_cache-> the prev fatcache of OUTPUT
 *OUTPUT:
 *  return a fatcache which contain cluster cl
 *NOTE:
 * if *prev_cache = return cache,that means the cache we find in cluster fat is the first one
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
 *Function Find_nextclus
 *PURPUSE: find the next cluster number of clus
 *PARAMETERS:
 * fat -> pointer to a cluster fat descripor
 * clus -> find the next cluster of cluster number clus
 * cl -> the next cluster number will returned
 *OUTPUT:
 *  return a fatcache which contain the next cluster
 *NOTE:
 * if returned fatcache is null and *cl = 0 ,that means DON'T find the next cluster from the given cluster fat
 * if returned fatcache is null but *cl != 0 ,that means clus is the last cluster of the given cluster fat
 */
struct fatcache* Find_nextclus(struct cluster_chain_descriptor* fat,unsigned int clus, unsigned int* cl)
{
	struct fatcache* cache = fat->child;
	*cl = 0x0;
	if(!cache){
		fsck_warn("Not find the cluster after cluster %d\n",clus);
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

/*
 *Function delete_fatcache_below
 *PURPUSE: delete all the fatcache below a given fatcache in a given cluster fat
 *PARAMETERS:
 * fatentry -> pointer to a cluster fat descripor
 * cache -> the fatcache whose below fatcache will be removed
 */
int delete_fatcache_below(struct cluster_chain_descriptor * fatentry,struct fatcache*cache)
{
	struct fatcache *curr = cache,*next,*last;
	struct fragment *frag,*insert;

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
		frag = New_fragment();
		if(!frag){
			fsck_err("%s: No space left\n",__func__);
			goto free;
		}
		/*when clear chain or Trunc ,move this cluster cache to free tree for writefat()*/
		frag->head = curr->head;
		curr->length = curr->length;
		insert = RB_INSERT(FSCK_MSDOS_FRAGMENT,&rb_free_root,frag);
		if(insert)
		fsck_warn("%s:fragment(head:0x%x) exist\n",__func__,frag->head);
free:
		free((void*)curr);
	}
	last->next = NULL;
	return 0;
}

/*
 *Function Trunc
 *PURPUSE: delete all the clusters after cl from a given cluster fat
 *PARAMETERS:
 * fat -> pointer to a cluster fat descripor
 * cl -> the cluster whose below clusters will be removed
 *NOTE: this function was used to handle the issue when a file has incorrect cluster numbers
 */
void Trunc(struct bootblock *boot, struct cluster_chain_descriptor *fat, unsigned int cl)
{
	struct fatcache *prev , *cache = Find_cache(fat,cl,&prev);
	unsigned int currlen = 0,org_chain_len = fat->length;
	struct fragment *frag,*insert;
	fsck_info("cluster chain :%p ,cl : %d \n",fat,cl);

	if(!cache)
		return;
	delete_fatcache_below(fat,cache);
	currlen = cl - cache->head + 1;
	if(currlen != cache->length){
		frag = New_fragment();
		if(!frag){
			fsck_err("%s ,No space left\n",__func__);
			goto re_calc;
		}
		frag->head = cl + 1;
		frag->length = cache->length - currlen;
		insert = RB_INSERT(FSCK_MSDOS_FRAGMENT,&rb_free_root,frag);
		if(insert)
		fsck_info("%s:fragment(head:0x%x) exist\n",__func__,frag->head);
   }
re_calc:
	fat->length -= (cache->length - currlen);
	cache->length = currlen;
	/*re-calc Numfree*/
	boot->NumFree += (org_chain_len - fat->length);
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
