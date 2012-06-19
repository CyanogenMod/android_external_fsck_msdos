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
#include "fragment.h"
#include "malloc.h"
#include "fatcache.h"
static int fsck_msdos_fragment_compare(struct fragment *frag1 ,struct fragment *frag2)
{
	if(frag1->head > frag2->head)
         return 1;
    else if(frag1->head < frag2->head)
         return -1;
    else
         return 0;
}
RB_GENERATE(FSCK_MSDOS_FRAGMENT,fragment,rb,fsck_msdos_fragment_compare);
struct fragment* New_fragment(void)
{
    struct fragment *frag;
    frag = calloc(1,sizeof(struct fragment));
    if(!frag){
        fsck_warn("%s,No space left \n",__func__);
        return EMPTY_FRAGMENT;
    }
	RB_SET(frag,NULL,rb);
    return frag;
}


void free_fragment_tree(struct FSCK_MSDOS_FRAGMENT* head)
{
	struct fragment * frag,*next_frag;
	/*
	 *avoid using function RB_FOREACH here
	 *RB_FOREACH(frag, FSCK_MSDOS_FRAGMENT,head)
	 *	free(frag);
	 * is dangerous here
	 */
	fsck_info("free_fragment_tree\n");
	frag = RB_MIN(FSCK_MSDOS_FRAGMENT,head);
	if(!frag){
		fsck_info("%s: rb_tree is empty \n",__func__);
		return ;
	}
	while(frag){
		next_frag = RB_NEXT(FSCK_MSDOS_FRAGMENT,0,frag);
		/*before free it ,must remove it from the rb_tree*/
		RB_REMOVE(FSCK_MSDOS_FRAGMENT,head,frag);
		free(frag);
		frag = next_frag;
	}
}

struct FSCK_MSDOS_FRAGMENT rb_free_root,rb_bad_root;
