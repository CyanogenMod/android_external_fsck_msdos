/*
 * Copyright (C) 1995, 1996, 1997 Wolfgang Solfrank
 * Copyright (c) 1995 Martin Husemann
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Martin Husemann
 *	and Wolfgang Solfrank.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/cdefs.h>
#include <assert.h>
#ifndef lint
__RCSID("$NetBSD: fat.c,v 1.12 2000/10/10 20:24:52 is Exp $");
static const char rcsid[] =
  "$FreeBSD: src/sbin/fsck_msdosfs/fat.c,v 1.9 2008/01/31 13:22:13 yar Exp $";
#endif /* not lint */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>

#include "ext.h"
#include "fsutil.h"
#include "fatcache.h"
#include "fragment.h"
static int checkclnum(struct bootblock *, cl_t, cl_t *);
static int clustdiffer(cl_t, cl_t *, cl_t *, int,int *);
static int tryclear(struct cluster_chain_descriptor *, cl_t, cl_t *);
int _readfat(int, struct bootblock *, int, u_char **);

static cl_t firstfreecl = 0xFFFFFFFF;
/*-
 * The first 2 FAT entries contain pseudo-cluster numbers with the following
 * layout:
 *
 * 31...... ........ ........ .......0
 * rrrr1111 11111111 11111111 mmmmmmmm         FAT32 entry 0
 * rrrrsh11 11111111 11111111 11111xxx         FAT32 entry 1
 * 
 *                   11111111 mmmmmmmm         FAT16 entry 0
 *                   sh111111 11111xxx         FAT16 entry 1
 * 
 * r = reserved
 * m = BPB media ID byte
 * s = clean flag (1 = dismounted; 0 = still mounted)
 * h = hard error flag (1 = ok; 0 = I/O error)
 * x = any value ok
 */

int
checkdirty(int fs, struct bootblock *boot)
{
	off_t off;
	u_char *buffer;
	int ret = 0;

	if (boot->ClustMask != CLUST16_MASK && boot->ClustMask != CLUST32_MASK)
		return 0;

	off = boot->ResSectors;
	off *= boot->BytesPerSec;

	buffer = malloc(boot->BytesPerSec);
	if (buffer == NULL) {
		perror("No space for FAT");
		return 1;
	}

	if (lseek(fs, off, SEEK_SET) != off) {
		perror("Unable to read FAT");
		goto err;
	}

	if (read(fs, buffer, boot->BytesPerSec) != boot->BytesPerSec) {
		perror("Unable to read FAT");
		goto err;
	}

	/*
	 * If we don't understand the FAT, then the file system must be
	 * assumed to be unclean.
	 */
	if (buffer[0] != boot->Media || buffer[1] != 0xff)
		goto err;
	if (boot->ClustMask == CLUST16_MASK) {
		if ((buffer[2] & 0xf8) != 0xf8 || (buffer[3] & 0x3f) != 0x3f)
			goto err;
	} else {
		if (buffer[2] != 0xff || (buffer[3] & 0x0f) != 0x0f
		    || (buffer[4] & 0xf8) != 0xf8 || buffer[5] != 0xff
		    || buffer[6] != 0xff || (buffer[7] & 0x03) != 0x03)
			goto err;
	}

	/*
	 * Now check the actual clean flag (and the no-error flag).
	 */
	if (boot->ClustMask == CLUST16_MASK) {
		if ((buffer[3] & 0xc0) == 0xc0)
			ret = 1;
	} else {
		if ((buffer[7] & 0x0c) == 0x0c)
			ret = 1;
	}

err:
	free(buffer);
	return ret;
}

/*
 * Check a cluster number for valid value
 */
static struct fragment *lastfree= NULL;
static struct fragment *lastbad= NULL;
int checkclnum(struct bootblock *boot, cl_t cl, cl_t *next)
{
	struct fragment *frag;
	struct fragment *insert;
	if((*next >= (CLUST_EOFS & boot->ClustMask)) && (*next <= (CLUST_EOF & boot->ClustMask)))
		return FSOK;
	if(*next >= (CLUST_RSRVD & boot->ClustMask)){
		*next |= ~boot->ClustMask;
	}
	/*if it is a free cluster,add to rb_free_root tree*/
	if (*next == CLUST_FREE) {
		boot->NumFree++;
		/*find the first free cluster ,this value will be used to update fsinfo*/
		if(firstfreecl > cl)
			firstfreecl = cl;
		if(lastfree){
			if( cl == (lastfree->head + lastfree->length)){
				lastfree->length  += 1;
				return FSOK;
			}
		}
		frag = New_fragment();
		if(!frag){
			fsck_info("No space \n");
			return FSERROR;
		}
		frag->head = cl;
		frag->length = 1;
		insert = RB_INSERT(FSCK_MSDOS_FRAGMENT,&rb_free_root,frag);
		if(insert){
			fsck_info("%s:fragment(head:0x%x) exist\n",__func__,frag->head);
			return FSERROR;
		}
		lastfree = frag;
		return FSOK;
	}
	/*if it is a bad cluster ,add to rb_bad_root tree*/
	if (*next == CLUST_BAD) {
		boot->NumBad++;
		if(lastbad){
			if(cl == (lastbad->head + lastbad->length)){
				lastbad->length  += 1;
				return FSOK;
			}
		}
		frag = New_fragment();
		if(!frag){
			fsck_info("No Space\n");
			return FSERROR;
		}
		frag->head = cl;
		frag->length = 1;
		insert = RB_INSERT(FSCK_MSDOS_FRAGMENT,&rb_bad_root,frag);
		if(insert){
			fsck_info("%s:fragment(head:0x%x) exist\n",__func__,frag->head);
			return FSERROR;
		}
		lastbad = frag;
		return FSOK;
	}
	if (*next < CLUST_FIRST || (*next >= boot->NumClusters && *next < (CLUST_EOFS & boot->ClustMask))) {
		pwarn("Cluster %u in FAT continues with %s cluster number %u\n",
		      cl,
		      *next < CLUST_RSRVD ? "out of range" : "reserved",
		      *next&boot->ClustMask);
		if (ask(1, "Truncate")) {
			/*do nothing about truncate*/
			return FSFATMOD;
		}
		return FSERROR;
	}
	return FSOK;
}

/*
 * Read a FAT from disk. Returns 1 if successful, 0 otherwise.
 */
int
_readfat(int fs, struct bootblock *boot, int no, u_char **buffer)
{
	off_t off;

        printf("Attempting to allocate %u KB for FAT\n",
                (boot->FATsecs * boot->BytesPerSec) / 1024);

	*buffer = malloc(boot->FATsecs * boot->BytesPerSec);
	if (*buffer == NULL) {
		perror("No space for FAT");
		return 0;
	}

	off = boot->ResSectors + no * boot->FATsecs;
	off *= boot->BytesPerSec;

	if (lseek(fs, off, SEEK_SET) != off) {
		perror("Unable to read FAT");
		goto err;
	}

	if (read(fs, *buffer, boot->FATsecs * boot->BytesPerSec)
	    != boot->FATsecs * boot->BytesPerSec) {
		perror("Unable to read FAT");
		goto err;
	}

	return 1;

    err:
	free(*buffer);
	return 0;
}

/*
 * Read a FAT and decode it into internal format
 */
unsigned int *fat_bitmap;
int checkfat(int fs, struct bootblock *boot, int no,u_char *buffer)
{
	struct cluster_chain_descriptor *fat  = NULL,*nextcl_fat,*insert,*remove,tofind;
	struct fatcache *cache = NULL;
	u_char *p;
	cl_t cl,prevcl = 0, nextclus;
	int len = 0;
	int ret = FSOK,err = 0;
	boot->NumFree = boot->NumBad = 0;
	fat_bitmap = calloc(boot->NumClusters/32+1,sizeof(unsigned int));
	if(!fat_bitmap){
		fsck_err("NO space left\n");
		return FSFATAL;
	}
	if (buffer[0] != boot->Media
	    || buffer[1] != 0xff || buffer[2] != 0xff
	    || (boot->ClustMask == CLUST16_MASK && buffer[3] != 0xff)
	    || (boot->ClustMask == CLUST32_MASK
		&& ((buffer[3]&0x0f) != 0x0f
		    || buffer[4] != 0xff || buffer[5] != 0xff
		    || buffer[6] != 0xff || (buffer[7]&0x0f) != 0x0f))) {

		/* Windows 95 OSR2 (and possibly any later) changes
		 * the FAT signature to 0xXXffff7f for FAT16 and to
		 * 0xXXffff0fffffff07 for FAT32 upon boot, to know that the
		 * file system is dirty if it doesn't reboot cleanly.
		 * Check this special condition before errorring out.
		 */
		if (buffer[0] == boot->Media && buffer[1] == 0xff
		    && buffer[2] == 0xff
		    && ((boot->ClustMask == CLUST16_MASK && buffer[3] == 0x7f)
			|| (boot->ClustMask == CLUST32_MASK
			    && buffer[3] == 0x0f && buffer[4] == 0xff
			    && buffer[5] == 0xff && buffer[6] == 0xff
			    && buffer[7] == 0x07)))
			ret |= FSDIRTY;
		else {
			/* just some odd byte sequence in FAT */
				
			switch (boot->ClustMask) {
			case CLUST32_MASK:
				pwarn("%s (%02x%02x%02x%02x%02x%02x%02x%02x)\n",
				      "FAT starts with odd byte sequence",
				      buffer[0], buffer[1], buffer[2], buffer[3],
				      buffer[4], buffer[5], buffer[6], buffer[7]);
				break;
			case CLUST16_MASK:
				pwarn("%s (%02x%02x%02x%02x)\n",
				    "FAT starts with odd byte sequence",
				    buffer[0], buffer[1], buffer[2], buffer[3]);
				break;
			default:
				pwarn("%s (%02x%02x%02x)\n",
				    "FAT starts with odd byte sequence",
				    buffer[0], buffer[1], buffer[2]);
				break;
			}

	
			if (ask(1, "Correct"))
				ret |= FSFIXFAT;
		}
	}
	fsck_info("Begin to handle the cluster chain\n");
	for (cl = CLUST_FIRST; cl < boot->NumClusters;cl++) {
		/*have handled it by cluster chain*/
		if(BIT(fat_bitmap[cl/32],cl%32))
			continue;
		prevcl = nextclus = cl;
		len = 0;
		switch (boot->ClustMask) {
			case CLUST32_MASK:
				p = buffer + 4*cl;
				break;
			case CLUST16_MASK:
				p = buffer + 2*cl;
				break;
			default:
				p = buffer + 3*(cl/2);
				break;
		}
		while(nextclus >= CLUST_FIRST && nextclus < boot->NumClusters){

			switch (boot->ClustMask) {
			case CLUST32_MASK:
				nextclus = p[0] + (p[1] << 8)
					       + (p[2] << 16) + (p[3] << 24);
				nextclus &= boot->ClustMask;
				p = buffer + 4*nextclus;
				break;
			case CLUST16_MASK:
				nextclus = p[0] + (p[1] << 8);
				p = buffer + 2*nextclus;
				break;
			/*FAT12 is special*/
			default:
				/* odd */
				if(!(nextclus & 0x1))
					nextclus = (p[0] + (p[1] << 8)) & 0x0fff;
				else
					nextclus = ((p[1] >> 4) + (p[2] << 4)) & 0x0fff;
				p = buffer + 3*(nextclus/2);
				break;
			}
			ret = checkclnum(boot,prevcl,&nextclus);
			if(ret == FSERROR)
				return -1;
			//truncate the rest clusters
			if(ret == FSFATMOD)
				break;
			len++;
			/*
			 *TODO:because this cluster chain is special,such as
			 *fatentry->fatcache->fatcache->fatcache->NULL
			 *so you should treat the first fatcache specially
			 */
			if(len == 1){
				if(nextclus == CLUST_FREE || nextclus == CLUST_BAD ){
					SET_BIT(fat_bitmap[prevcl/32],prevcl%32);
					break;
				}
			}
			/*last cluster*/
			if((nextclus >= (CLUST_EOFS & boot->ClustMask)) && (nextclus <= (CLUST_EOF & boot->ClustMask)) && (len != 1)){
				SET_BIT(fat_bitmap[prevcl/32],prevcl%32);
				break;
			}
			/*rsrvd*/
			if(nextclus == CLUST_FREE || ((nextclus >= (CLUST_RSRVD & boot->ClustMask)) && (nextclus < (CLUST_EOFS & boot->ClustMask)))){
				fsck_warn("Cluster chain starting at %u ends with cluster marked %s,cl = %d ,nextclus = %d\n",cl,rsrvdcltype(nextclus),cl,nextclus);
				SET_BIT(fat_bitmap[prevcl/32],prevcl%32);
				/*if the next cluster is free or reversed ,just clear the existing cluster chain ,let this free or reversed cluster alone*/
				ret |= tryclear(fat,cl,&prevcl);
				break;
			}
			/*out of range*/
			if(nextclus < CLUST_FIRST ||(( nextclus >= boot->NumClusters) && (nextclus < (CLUST_EOFS & boot->ClustMask)))){
				fsck_warn("Clusters chain starting at %u ends with cluster out of range (%u) \n",cl,nextclus);
				SET_BIT(fat_bitmap[prevcl/32],prevcl%32);
				ret |= tryclear(fat,cl,&prevcl);
				break;
			}

			if((nextclus >= CLUST_FIRST && nextclus < boot->NumClusters)&& BIT(fat_bitmap[nextclus/32],nextclus%32)){
				/*
				 *it is interesting,because 0x3->0x34->0x35->EOF,then you find 0x100->0x3.
				 *it is right here,must merge them
				 */
				tofind.head = nextclus;
				nextcl_fat = RB_FIND(FSCK_MSDOS_CACHE,&rb_root,&tofind);
				/*if nextcl_fat == fat ,unfortunately ,it is a cluster loop,such as 0x2->0x3->0x4->0x2*/
				if(nextcl_fat && (nextcl_fat != fat)){
					if(len == 1){
						/*the first one ,you should malloc both fatEntry and fatcache*/
						fat = New_fatentry();
						if(!fat)
							return FSFATAL;
						fat->head = prevcl;
						fat->length = 1;
						/*
						 *i was puzzled whether check the pointer insert here
						 *if some error,there is a exsting fatentry in rb tree.NOT check maybe a good choice
						 */
						insert = RB_INSERT(FSCK_MSDOS_CACHE,&rb_root,fat);
						if(insert){
							fsck_err("%s:fatentry(head:0x%x) exist\n",__func__,fat->head);
							return FSFATAL;
						}
						cache = New_fatcache();
						if(!cache)
							return FSFATAL;
						cache->head = prevcl;
						cache->length = 1;
						err = add_fatcache_To_ClusterChain(fat,cache);
						if(err){
							fsck_err("add_fatcache_To_ClusterChain ,err = %d \n",err);
							return FSFATAL;
						}
						/*merge nextcl_fat chain to new  one*/
						cache->next = nextcl_fat->child;
						fat->length += nextcl_fat->length;
					}else{
						/*
						 *we use fat directly here,after handling a cluster chain ,must set fat = NULL,
						 *otherwise ,the fat is prev cluster chain
						 */
						add_fatcache_Totail(fat,nextcl_fat->child);
						fat->length += nextcl_fat->length;
					}
					nextcl_fat->child = NULL;
					/*now delete nextcl_fat entry ,because have merge its child to fat*/
					remove = RB_REMOVE(FSCK_MSDOS_CACHE,&rb_root,nextcl_fat);
					free(nextcl_fat);
					SET_BIT(fat_bitmap[prevcl/32],prevcl%32);
					break;
				}
				/*
				 *it is very complex here,if a cluster has been handled ,but it is not a head of a cluster chain,there are some cases:
				 *case 1:Due to some error ,this cluster has been removed from rb tree by tryclear()
				 *case 2:clsuter chains linked
				 *case 3:cluster loop
				 *whatever ,i think tryclear() is a simplest solution
				 */
				fsck_err("Cluster chian is wrong\n");
				Dump_fatentry(fat);
				Dump_fatentry(nextcl_fat);
				/*sometimes ,fat == NULL here,it means this cluster chain haven't be insert to rb tree,ok,do nothing here*/
				if(!fat)
					break;
				if(ask(1,"Clear chain starting at %u",nextclus)){
					/*
					 *compare with orginal code,i delete some codes here
					 *because i handle fat chain by chain here,
					 *it never appear  situation  that three or more cluster chains linked
					 */
					ret |= tryclear(fat,cl,&prevcl);
					break;
				}
			}
			/*it is a legal cluster ,add it*/
			SET_BIT(fat_bitmap[prevcl/32],prevcl%32);
			if(len ==  1){
			/*TODO:
			 *that is the first cluster of this legal chain
			 *it is special ,normally, we just add nextcluster to chain
			 *but if it is first one ,we must add both two.
			 */
				fat = New_fatentry();
				if(!fat){
					fsck_err("No space \n");
					return FSFATAL;
				}
				fat->head = cl;
				insert = RB_INSERT(FSCK_MSDOS_CACHE,&rb_root,fat);
				if(insert){
					fsck_err("%s:fatentry(head:0x%x) exist\n",__func__,fat->head);
					return FSFATAL;
				}
				cache = New_fatcache();
				if(!cache){
					fsck_err("No space \n");
					return FSFATAL;
				}
				cache->head = prevcl;
				cache->length = 1;
				err = add_fatcache_To_ClusterChain(fat,cache);
				if(err){
					fsck_err("add_fatcache_To_ClusterChain ,err = %d \n",err);
				}
				if(nextclus >= (CLUST_EOFS & boot->ClustMask) && nextclus <= (CLUST_EOF & boot->ClustMask)){
					break;
				}
				if(prevcl + 1 == nextclus){
					cache->length += 1;
					fat->length += 1;
					prevcl = nextclus;
					continue;
				}else{
					cache = New_fatcache();
					if(!cache){
						fsck_err("No space\n");
						return FSFATAL;
					}
					cache->head = nextclus;
					cache->length = 1;
					err = add_fatcache_To_ClusterChain(fat,cache);
					if(err){
						fsck_err("add_fatcache_To_ClusterChain ,err = %d \n",err);
					}
				}
			}else{
				if(nextclus >= (CLUST_EOFS & boot->ClustMask) && nextclus <= (CLUST_EOF & boot->ClustMask)){
					break;
				}
				if( prevcl + 1 == nextclus){
					/*if it is continuous ,just modify the length*/
					cache->length += 1;
					fat->length += 1;
				}else{
					cache = New_fatcache();
					if(!cache){
						fsck_err("No Space\n");
						return FSFATAL;
					}
					cache->head = nextclus;
					cache->length = 1;
					err = add_fatcache_To_ClusterChain(fat,cache);
					if(err){
						fsck_err("add_fatcache_To_ClusterChain ,err = %d \n",err);
					}
				}
			}
			prevcl = nextclus;
		}
		/*set fat = NULL,otherwise this fat pointer will impact the next process*/
		fat = NULL;
	}
#if 0
	fsck_info("Dump cluster chains\n");
	RB_FOREACH(fat, FSCK_MSDOS_CACHE,&rb_root){
		fsck_info("head:0x%x:length:0x%x\n",fat->head,fat->length);
	}
#endif
	free(fat_bitmap);
	return ret;
}

/*
 * Get type of reserved cluster
 */
char *
rsrvdcltype(cl_t cl)
{
	if (cl == CLUST_FREE)
		return "free";
	if (cl < CLUST_BAD)
		return "reserved";
	if (cl > CLUST_BAD)
		return "as EOF";
	return "bad";
}
/*
 *if *res = 1,that means *cp1 = *cp2
 *if *res = 0 ,means *cp2 = *cp1;
 */
static int
clustdiffer(cl_t cl, cl_t *cp1, cl_t *cp2, int fatnum, int *res)
{
	fsck_debug("clustdiff:%u : %u \n",*cp1,*cp2);
	if (*cp1 == CLUST_FREE || *cp1 >= CLUST_RSRVD) {
		if (*cp2 == CLUST_FREE || *cp2 >= CLUST_RSRVD) {
			if ((*cp1 != CLUST_FREE && *cp1 < CLUST_BAD
			     && *cp2 != CLUST_FREE && *cp2 < CLUST_BAD)
			    || (*cp1 > CLUST_BAD && *cp2 > CLUST_BAD)) {
				pwarn("Cluster %u is marked %s with different indicators\n",
				      cl, rsrvdcltype(*cp1));
				if (ask(1, "Fix")) {
					*res = 0;
					return FSFATMOD;
				}
				return FSFATAL;
			}
			pwarn("Cluster %u is marked %s in FAT 0, %s in FAT %d\n",
			      cl, rsrvdcltype(*cp1), rsrvdcltype(*cp2), fatnum);
			if (ask(1, "Use FAT 0's entry")) {
				*res = 0;
				return FSFATMOD;
			}
			if (ask(1, "Use FAT %d's entry", fatnum)) {
				*res = 1;
				return FSFATMOD;
			}
			return FSFATAL;
		}
		pwarn("Cluster %u is marked %s in FAT 0, but continues with cluster %u in FAT %d\n",
		      cl, rsrvdcltype(*cp1), *cp2, fatnum);
		if (ask(1, "Use continuation from FAT %d", fatnum)) {
			*res = 1;
			return FSFATMOD;
		}
		if (ask(1, "Use mark from FAT 0")) {
			*res = 0;
			return FSFATMOD;
		}
		return FSFATAL;
	}
	if (*cp2 == CLUST_FREE || *cp2 >= CLUST_RSRVD) {
		pwarn("Cluster %u continues with cluster %u in FAT 0, but is marked %s in FAT %d\n",
		      cl, *cp1, rsrvdcltype(*cp2), fatnum);
		if (ask(1, "Use continuation from FAT 0")) {
			*res = 0;
			return FSFATMOD;
		}
		if (ask(1, "Use mark from FAT %d", fatnum)) {
			*res = 1;
			return FSFATMOD;
		}
		return FSERROR;
	}
	pwarn("Cluster %u continues with cluster %u in FAT 0, but with cluster %u in FAT %d\n",
	      cl, *cp1, *cp2, fatnum);
	if (ask(1, "Use continuation from FAT 0")) {
		*res = 0;
		return FSFATMOD;
	}
	if (ask(1, "Use continuation from FAT %d", fatnum)) {
		*res = 1;
		return FSFATMOD;
	}
	return FSERROR;
}

/*
 * Compare two FAT copies in memory. Resolve any conflicts and merge them
 * into the first one.
 */
int
comparefat(struct bootblock *boot, u_char*first, u_char  *second, int fatnum)
{
	cl_t cl;
	u_char *sp,*fp,*wp;
	int ret = FSOK,res = 0;
	cl_t first_cl,second_cl,w_cl;
	fsck_info("Begin to compare FAT\n");
	for (cl = CLUST_FIRST; cl < boot->NumClusters; cl++){
			switch(boot->ClustMask){
				case CLUST32_MASK:
					fp = first + 4*cl;
					sp = second + 4*cl;
					first_cl = (fp[0] + (fp[1]<<8) + (fp[2]<<16) + (fp[3]<<24)) & boot->ClustMask;
					second_cl = (sp[0] + (sp[1]<<8) + (sp[2]<<16) + (sp[3]<<24)) & boot->ClustMask;
					break;

				case CLUST16_MASK:
					fp = first + 2*cl;
					sp = second + 2*cl;
					first_cl = (fp[0] + (fp[1] << 8)) & boot->ClustMask;
					second_cl = (sp[0] + (sp[1] << 8)) & boot->ClustMask;
					break;

				default:
					fp = first + 3*(cl/2);
					sp = second + 3*(cl/2);
					if(cl & 0x1){
						first_cl = ((fp[1]>>4) + (fp[2]<<4)) & 0x0fff;
						second_cl = ((sp[1]>>4) + (sp[2]<<4)) & 0x0fff;
					}else{
						first_cl = (fp[0] + (fp[1] << 8)) & 0x0fff;
						second_cl = (sp[0] + (sp[1] << 8)) & 0x0fff;
					}
					break;
			}
			if(first_cl == second_cl)
				continue;
			fsck_warn("%u is not same(%u ; %u)\n",cl,first_cl,second_cl);
			ret |= clustdiffer(cl, &first_cl,&second_cl, fatnum,&res);
			if(res){
				wp = fp;
				w_cl = second_cl;
			}else{
				wp = sp;
				w_cl = first_cl;
			}
			/* fisrt_cl = second_cl */
			switch(boot->ClustMask){
				case CLUST32_MASK:
					*wp++ = (u_char)w_cl;
					*wp++ = (u_char)(w_cl >>8);
					*wp++ = (u_char)(w_cl >> 16);
					*wp &= 0xf0;
					*wp |= (w_cl >>24) &0x0f;
					break;

				case CLUST16_MASK:
					*wp++ = (u_char)w_cl;
					*wp =  (u_char)(w_cl >> 8);
					break;

				default:
					if(cl & 0x1){
						/* wp[1] ,wp[2]*/
						wp++;
						*wp++ |= (u_char)((w_cl << 4) & 0xf0);
						*wp = (u_char)(w_cl >> 4);
					}else{
						/* wp[0] ,wp[1]*/
						*wp++ = (u_char)w_cl;
						*wp |= (u_char)((w_cl >> 8) & 0x0f);
					}
					break;
			}
	}
	return ret;
}

void
clearchain(struct cluster_chain_descriptor *fat, cl_t head)
{
	fsck_debug("%s:fat:%p , head(%d) ,length(%d)\n",__func__,fat,fat->head,fat->length);
	assert(fat);
	if(fat->head != head)
		return ;
	/*must remove from rb tree before free*/
	RB_REMOVE(FSCK_MSDOS_CACHE,&rb_root,fat);
	free(fat);
}

int
tryclear(struct cluster_chain_descriptor *fat, cl_t head, cl_t *trunc)
{
	fsck_debug("fat:%p ,head :%d ,trunc :%d \n",fat,head,*trunc);
	if(!fat || !trunc){
		pwarn("%s :null pointer\n",__func__);
		return FSERROR;
	}
	if (ask(1, "Clear chain starting at %u", head)) {
		clearchain(fat, head);
		return FSFATMOD;
	} else if (ask(1, "Truncate")) {
		Trunc(fat,*trunc);
		return FSFATMOD;
	} else
		return FSERROR;
}

/*
 * Write out FATs encoding them from the internal format
 */
int
writefat(int fs, struct bootblock *boot, int correct_fat)
{
	u_char *buffer, *p;
	cl_t cl;
	unsigned int i;
	u_int32_t fatsz;
	off_t off;
	int ret = FSOK;
	struct cluster_chain_descriptor *fat;
	struct fatcache* cache,*next_cache;
	struct fragment *frag;
	fsck_debug("begin to writefat \n");
	buffer = malloc(fatsz = boot->FATsecs * boot->BytesPerSec);
	if (buffer == NULL) {
		perror("No space for FAT");
		return FSFATAL;
	}
	memset(buffer, 0, fatsz);
	p = buffer;
	if (correct_fat) {
		*p++ = (u_char)boot->Media;
		*p++ = 0xff;
		*p++ = 0xff;
		switch (boot->ClustMask) {
		case CLUST16_MASK:
			*p++ = 0xff;
			break;
		case CLUST32_MASK:
			*p++ = 0x0f;
			*p++ = 0xff;
			*p++ = 0xff;
			*p++ = 0xff;
			*p++ = 0x0f;
			break;
		}
	} else {
		/* use same FAT signature as the old FAT has */
		int count;
		u_char *old_fat;

		switch (boot->ClustMask) {
		case CLUST32_MASK:
			count = 8;
			break;
		case CLUST16_MASK:
			count = 4;
			break;
		default:
			count = 3;
			break;
		}

		if (!_readfat(fs, boot, boot->ValidFat >= 0 ? boot->ValidFat :0,
					 &old_fat)) {
			free(buffer);
			return FSFATAL;
		}

		memcpy(p, old_fat, count);
		free(old_fat);
		p += count;
	}

	fsck_info("begin to write FAT\n");
	fat = RB_MIN(FSCK_MSDOS_CACHE,&rb_root);
	if(!fat){
		fsck_info("%s:rb tree is empty\n",__func__);
		return FSFATAL;
	}
	fsck_info("write valid cluster chain\n");
	while(fat){
		cache = fat->child;
		while(cache){
			cl = cache->head;
			for( i = 0 ; i < (cache->length - 1);i++)
				SetNextClusToFAT(boot,buffer,cl+i ,cl+i+1);
			next_cache = cache->next;
			if(next_cache)
				SetNextClusToFAT(boot,buffer,cl+cache->length -1,next_cache->head);
			else
				SetNextClusToFAT(boot,buffer,cl+cache->length -1,CLUST_EOF);
			cache = next_cache;
		}
		fat = RB_NEXT(FSCK_MSDOS_CACHE,0,fat);
	}
	/*the FreeNumber may be not correct*/
	fsck_info("write free cluster\n");
	RB_FOREACH(frag, FSCK_MSDOS_FRAGMENT,&rb_free_root){
		cl = frag->head;
		for(i = 0 ; i < frag->length ;i++)
			SetNextClusToFAT(boot,buffer,cl+i ,CLUST_FREE);
	}
	fsck_info("write bad cluster\n");
	RB_FOREACH(frag, FSCK_MSDOS_FRAGMENT,&rb_bad_root){
		cl = frag->head;
		for(i = 0 ; i < frag->length ;i++)
			SetNextClusToFAT(boot,buffer,cl+i ,CLUST_BAD);
	}
	fsck_debug("write FATs\n");
	for (i = 0; i < boot->FATs; i++) {
		fsck_debug("write FAT copy %d \n",i);
		off = boot->ResSectors + i * boot->FATsecs;
		off *= boot->BytesPerSec;
		if (lseek(fs, off, SEEK_SET) != off
		    || write(fs, buffer, fatsz) != fatsz) {
			perror("Unable to write FAT");
			ret = FSFATAL; /* Return immediately?		XXX */
		}
	}
	free(buffer);
	return ret;
}

/*
 * Check a complete in-memory FAT for lost cluster chains
 */
int
checklost(int dosfs, struct bootblock *boot)
{
	int mod = FSOK;
	int ret;
	struct cluster_chain_descriptor *fat ;
	fat = RB_MIN(FSCK_MSDOS_CACHE,&rb_root);
	if(!fat){
		fsck_err("%s:rb_root tree is empty\n",__func__);
		return FSFATAL;
	}
	while(fat){
		if(fat->flag & FAT_USED){
			fat  = RB_NEXT(FSCK_MSDOS_CACHE,0,fat);
			continue;
		}
		fsck_info("Lost cluster chain at head %u ,%d Cluster(s) lost\n",fat->head, fat->length);
		mod |= ret = reconnect(dosfs, boot,fat, fat->head);
		if (mod & FSFATAL) {
			/* If the reconnect failed, then just clear the chain */
			pwarn("Error reconnecting chain - clearing\n");
			mod &= ~FSFATAL;
			clearchain(fat,fat->head);
			mod |= FSFATMOD;
			fat  = RB_NEXT(FSCK_MSDOS_CACHE,0,fat);
			continue;
		}
		if (ret == FSERROR && ask(1, "Clear")) {
			clearchain(fat, fat->head);
			mod |= FSFATMOD;
		}
		fat  = RB_NEXT(FSCK_MSDOS_CACHE,0,fat);
	}
	finishlf();
	fsck_debug("Verify Filesystem information\n");
	//verify the fs infomation
	if (boot->FSInfo){
		ret = 0;
		if (boot->FSFree != boot->NumFree) {
			pwarn("Free space in FSInfo block (%d) not correct (%d)\n",
			      boot->FSFree, boot->NumFree);
			if (ask(1, "Fix")) {
				boot->FSFree = boot->NumFree;
				ret = 1;
			}
		}

		if (boot->NumFree) {
			if ((boot->FSNext >= boot->NumClusters)) {
				pwarn("Next free cluster in FSInfo block out of range\n");
				if (ask(1, "Fix")){
					boot->FSNext = firstfreecl;
					ret = 1;
				}
			}
		}
		if (boot->FSNext > boot->NumClusters  || boot->FSNext < CLUST_FIRST) {
			pwarn("FSNext block (%d) not correct NumClusters (%d)\n",
					boot->FSNext, boot->NumClusters);
			boot->FSNext=CLUST_FIRST; // boot->FSNext can have -1 value.
		}
		if (ret)
			mod |= writefsinfo(dosfs, boot);
	}

	return mod;
}
