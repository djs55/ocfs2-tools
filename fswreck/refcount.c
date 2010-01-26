/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * refcount.c
 *
 * Copyright (C) 2009 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <assert.h>
#include "main.h"

extern char *progname;

/*
 * Create a refcount tree.
 *
 * If tree = 0, the root is just a refcount block with refcount recs.
 * Otherwise, we will create a refcount extent tree.
 *
 */
static void create_refcount_tree(ocfs2_filesys *fs, uint64_t blkno,
				 uint64_t *rf_blkno, int tree_depth)
{
	errcode_t ret;
	uint64_t file1, file2, file3, root_blkno, new_clusters, tmpblk;
	int i, recs_num = ocfs2_refcount_recs_per_rb(fs->fs_blocksize);
	int bpc = ocfs2_clusters_to_blocks(fs, 1), offset = 0;
	uint32_t n_clusters;
	uint64_t file_size;

	/*
	 * Create 3 files.
	 * file1 and file2 are used to sharing a refcount tree.
	 * file2 is used to waste some clusters so that the refcount
	 * tree won't be increased easily.
	 */
	create_file(fs, blkno, &file1);
	create_file(fs, blkno, &file2);
	create_file(fs, blkno, &file3);

	ret = ocfs2_new_refcount_block(fs, &root_blkno, 0, 0);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	/* attach the create refcount tree in these 2 files. */
	ret = ocfs2_attach_refcount_tree(fs, file1, root_blkno);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ret = ocfs2_attach_refcount_tree(fs, file2, root_blkno);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	/*
	 * Calculate how much clusters we need in order to create
	 * the required extent tree.
	 */
	new_clusters = 1;
	while (tree_depth-- > 0)
		new_clusters *= recs_num;
	new_clusters += recs_num / 2;

	/*
	 * We double the new_clusters so that half of them will be inserted
	 * into tree, while another half is inserted into file3.
	 */
	new_clusters *= 2;
	while (new_clusters) {
		ret = ocfs2_new_clusters(fs, 1, new_clusters, &blkno,
					 &n_clusters);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		if (!n_clusters)
			FSWRK_FATAL("ENOSPC");

		/*
		 * In order to ensure the extent records are not coalesced,
		 * we insert each cluster in reverse.
		 */
		for (i = n_clusters; i > 1; i -= 2, offset++) {
			tmpblk = blkno + ocfs2_clusters_to_blocks(fs, i - 2);
			ret = ocfs2_inode_insert_extent(fs, file1, offset,
							tmpblk, 1,
							OCFS2_EXT_REFCOUNTED);
			if (ret)
				FSWRK_COM_FATAL(progname, ret);
			ret = ocfs2_inode_insert_extent(fs, file2, offset,
							tmpblk, 1,
							OCFS2_EXT_REFCOUNTED);
			if (ret)
				FSWRK_COM_FATAL(progname, ret);

			ret = ocfs2_change_refcount(fs, root_blkno,
					ocfs2_blocks_to_clusters(fs, tmpblk),
					1, 2);
			if (ret)
				FSWRK_COM_FATAL(progname, ret);

			ret = ocfs2_inode_insert_extent(fs, file3, offset,
							tmpblk + bpc, 1, 0);
			if (ret)
				FSWRK_COM_FATAL(progname, ret);

		}
		new_clusters -= n_clusters;
	}

	file_size = (offset + 1) * fs->fs_clustersize;
	ret = ocfs2_extend_file(fs, file1, file_size);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ret = ocfs2_extend_file(fs, file2, file_size);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ret = ocfs2_extend_file(fs, file3, file_size);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);


	*rf_blkno = root_blkno;

	return;
}

static void damage_refcount_block(ocfs2_filesys *fs, enum fsck_type type,
				  struct ocfs2_refcount_block *rb)
{
	uint32_t oldno;
	uint64_t oldblkno;

	switch (type) {
	case RB_BLKNO:
		oldblkno = rb->rf_blkno;
		rb->rf_blkno += 1;
		fprintf(stdout, "RB_BLKNO: "
			"change refcount block's number from %"PRIu64" to "
			"%"PRIu64"\n", oldblkno, (uint64_t)rb->rf_blkno);
		break;
	case RB_GEN:
	case RB_GEN_FIX:
		oldno = rb->rf_fs_generation;
		rb->rf_fs_generation = 0x1234;
		if (type == RB_GEN)
			fprintf(stdout, "RB_GEN: ");
		else if (type == RB_GEN_FIX)
			fprintf(stdout, "RB_GEN_FIX: ");
		fprintf(stdout, "change refcount block %"PRIu64
			" generation number from 0x%x to 0x%x\n",
			(uint64_t)rb->rf_blkno, oldno, rb->rf_fs_generation);
		break;
	case RB_PARENT:
		oldblkno = rb->rf_parent;
		rb->rf_parent += 1;
		fprintf(stdout, "RB_PARENT: "
			"change refcount block's parent from %"PRIu64" to "
			"%"PRIu64"\n", oldblkno, (uint64_t)rb->rf_parent);
		break;
	case REFCOUNT_BLOCK_INVALID:
	case REFCOUNT_ROOT_BLOCK_INVALID:
		memset(rb->rf_signature, 'a', sizeof(rb->rf_signature));
		fprintf(stdout, "Corrupt the signature of refcount block "
			"%"PRIu64"\n", (uint64_t)rb->rf_blkno);
		break;
	default:
		FSWRK_FATAL("Invalid type=%d", type);
	}
}

void mess_up_refcount_tree_block(ocfs2_filesys *fs, enum fsck_type type,
				 uint64_t blkno)
{
	errcode_t ret;
	char *buf1 = NULL, *buf2 = NULL, *buf2_leaf = NULL;
	uint64_t rf_blkno1, rf_blkno2, rf_leaf_blkno;
	struct ocfs2_refcount_block *rb1, *rb2, *rb2_leaf;

	if (!ocfs2_refcount_tree(OCFS2_RAW_SB(fs->fs_super)))
		FSWRK_FATAL("Should specify a refcount supported "
			    "volume to do this corruption\n");

	ret = ocfs2_malloc_block(fs->fs_io, &buf1);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ret = ocfs2_malloc_block(fs->fs_io, &buf2);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ret = ocfs2_malloc_block(fs->fs_io, &buf2_leaf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	/*
	 * We create 2 refcount trees. One only has a root refcount block,
	 * and the other has a tree with depth = 1. So we can corrupt both
	 * of them and verify whether fsck works for different block types.
	 */
	create_refcount_tree(fs, blkno, &rf_blkno1, 0);
	create_refcount_tree(fs, blkno, &rf_blkno2, 1);

	ret = ocfs2_read_refcount_block(fs, rf_blkno1, buf1);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	rb1 = (struct ocfs2_refcount_block *)buf1;

	/* tree 2 is an extent tree, so find the 1st leaf refcount block. */
	ret = ocfs2_read_refcount_block(fs, rf_blkno2, buf2);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	rb2 = (struct ocfs2_refcount_block *)buf2;
	assert(rb2->rf_flags & OCFS2_REFCOUNT_TREE_FL);
	rf_leaf_blkno = rb2->rf_list.l_recs[0].e_blkno;
	ret = ocfs2_read_refcount_block(fs, rf_leaf_blkno, buf2_leaf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	rb2_leaf = (struct ocfs2_refcount_block *)buf2_leaf;

	switch (type) {
	case RB_BLKNO:
	case RB_GEN:
	case RB_GEN_FIX:
		damage_refcount_block(fs, type, rb1);
		damage_refcount_block(fs, type, rb2_leaf);
		break;
	case RB_PARENT:
		damage_refcount_block(fs, type, rb2_leaf);
		break;
	case REFCOUNT_BLOCK_INVALID:
		damage_refcount_block(fs, type, rb2_leaf);
		break;
	case REFCOUNT_ROOT_BLOCK_INVALID:
		damage_refcount_block(fs, type, rb1);
		damage_refcount_block(fs, type, rb2);
		break;
	default:
		FSWRK_FATAL("Invalid type[%d]\n", type);
	}

	ret = ocfs2_write_refcount_block(fs, rf_blkno1, buf1);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ret = ocfs2_write_refcount_block(fs, rf_blkno2, buf2);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ret = ocfs2_write_refcount_block(fs, rf_leaf_blkno, buf2_leaf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ocfs2_free(&buf1);
	ocfs2_free(&buf2);
	ocfs2_free(&buf2_leaf);

	return;
}