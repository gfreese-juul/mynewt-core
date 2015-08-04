#include <assert.h>
#include <string.h>
#include "ffs_priv.h"
#include "ffs/ffs.h"


/**
 * Calculates the amount of disk space required by the specified object.
 */
static uint32_t
ffs_gc_object_disk_size(const struct ffs_object *object)
{
    const struct ffs_inode *inode;
    const struct ffs_block *block;

    switch (object->fo_type) {
    case FFS_OBJECT_TYPE_INODE:
        inode = (void *)object;
        return sizeof (struct ffs_disk_inode) + inode->fi_filename_len;

    case FFS_OBJECT_TYPE_BLOCK:
        block = (void *)object;
        return sizeof (struct ffs_disk_block) + block->fb_data_len;

    default:
        assert(0);
        break;
    }
}

/**
 * Selects the most appropriate area for garbage collection.
 *
 * @return                  The ID of the area to garbage collect.
 */
static uint16_t
ffs_gc_select_area(void)
{
    const struct ffs_area *area;
    uint16_t best_area_idx;
    int8_t diff;
    int i;

    best_area_idx = 0;
    for (i = 1; i < ffs_num_areas; i++) {
        if (i == ffs_scratch_area_idx) {
            continue;
        }

        area = ffs_areas + i;
        if (area->fa_length > ffs_areas[best_area_idx].fa_length) {
            best_area_idx = i;
        } else if (best_area_idx == ffs_scratch_area_idx) {
            best_area_idx = i;
        } else {
            diff = ffs_areas[i].fa_gc_seq - ffs_areas[best_area_idx].fa_gc_seq;
            if (diff < 0) {
                best_area_idx = i;
            }
        }
    }

    assert(best_area_idx != ffs_scratch_area_idx);

    return best_area_idx;
}

static int
ffs_gc_block_chain(struct ffs_block *first_block, struct ffs_block *last_block,
                   uint32_t data_len, uint16_t to_area_idx)
{
    struct ffs_disk_block disk_block;
    struct ffs_area *to_area;
    struct ffs_block *block;
    struct ffs_block *next;
    uint32_t to_offset;
    int rc;

    to_area = ffs_areas + to_area_idx;

    memset(&disk_block, 0, sizeof disk_block);
    disk_block.fdb_magic = FFS_BLOCK_MAGIC;
    disk_block.fdb_id = first_block->fb_object.fo_id;
    disk_block.fdb_seq = first_block->fb_object.fo_seq + 1;
    disk_block.fdb_rank = first_block->fb_rank;
    disk_block.fdb_inode_id = first_block->fb_inode->fi_object.fo_id;
    disk_block.fdb_flags = first_block->fb_flags;
    disk_block.fdb_data_len = data_len;

    to_offset = to_area->fa_cur;
    rc = ffs_flash_write(to_area_idx, to_offset,
                         &disk_block, sizeof disk_block);
    if (rc != 0) {
        return rc;
    }

    block = first_block;
    while (1) {
        rc = ffs_flash_copy(block->fb_object.fo_area_idx,
                            block->fb_object.fo_area_offset + sizeof disk_block,
                            to_area_idx, to_area->fa_cur,
                            block->fb_data_len);
        if (rc != 0) {
            return rc;
        }

        block->fb_object.fo_area_idx = to_area_idx;
        block->fb_object.fo_area_offset = to_offset;

        next = SLIST_NEXT(block, fb_next);
        if (block != first_block) {
            block->fb_data_len = 0;
            ffs_block_delete_from_ram(block);
        }
        if (block == last_block) {
            break;
        }

        block = next;
    }

    first_block->fb_data_len = data_len;

    SLIST_NEXT(first_block, fb_next) = SLIST_NEXT(last_block, fb_next);

    return 0;
}

static int
ffs_gc_inode_blocks(struct ffs_inode *inode, uint16_t from_area_idx,
                    uint16_t to_area_idx)
{
    struct ffs_block *first_block;
    struct ffs_block *prev_block;
    struct ffs_block *block;
    uint32_t prospective_data_len;
    uint32_t data_len;
    int rc;

    assert(!(inode->fi_flags & FFS_INODE_F_DIRECTORY));

    first_block = NULL;
    prev_block = NULL;
    data_len = 0;
    SLIST_FOREACH(block, &inode->fi_block_list, fb_next) {
        if (block->fb_object.fo_area_idx == from_area_idx) {
            if (first_block == NULL) {
                first_block = block;
            }

            prospective_data_len = data_len + block->fb_data_len;
            if (prospective_data_len <= ffs_block_max_data_sz) {
                data_len = prospective_data_len;
            } else {
                rc = ffs_gc_block_chain(first_block, prev_block, data_len,
                                        to_area_idx);
                if (rc != 0) {
                    return rc;
                }
                first_block = block;
                data_len = block->fb_data_len;
            }
            prev_block = block;
        } else {
            if (first_block != NULL) {
                rc = ffs_gc_block_chain(first_block, prev_block, data_len,
                                        to_area_idx);
                if (rc != 0) {
                    return rc;
                }

                first_block = NULL;
                data_len = 0;
            }
            prev_block = NULL;
        }
    }

    if (first_block != NULL) {
        rc = ffs_gc_block_chain(first_block, prev_block, data_len,
                                to_area_idx);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

/**
 * Triggers a garbage collection cycle.
 *
 * @param out_area_idx      On success, the ID of the cleaned up area gets
 *                              written here.  Pass null if you do not need
 *                              this information.
 *
 * @return                  0 on success; nonzero on error.
 */
int
ffs_gc(uint16_t *out_area_idx)
{
    struct ffs_area *from_area;
    struct ffs_area *to_area;
    struct ffs_inode *inode;
    struct ffs_object *object;
    uint32_t to_offset;
    uint32_t obj_size;
    uint16_t from_area_idx;
    int rc;
    int i;

    from_area_idx = ffs_gc_select_area();
    from_area = ffs_areas + from_area_idx;

    rc = ffs_format_from_scratch_area(ffs_scratch_area_idx, from_area->fa_id);
    if (rc != 0) {
        return rc;
    }

    FFS_HASH_FOREACH(object, i) {
        if (object->fo_type == FFS_OBJECT_TYPE_INODE) {
            inode = (struct ffs_inode *)object;
            if (!(inode->fi_flags & FFS_INODE_F_DIRECTORY)) {
                rc = ffs_gc_inode_blocks(inode, from_area_idx,
                                         ffs_scratch_area_idx);
                if (rc != 0) {
                    return rc;
                }
            }
        }
    }

    to_area = ffs_areas + ffs_scratch_area_idx;
    FFS_HASH_FOREACH(object, i) {
        if (object->fo_area_idx == from_area_idx) {
            obj_size = ffs_gc_object_disk_size(object);
            to_offset = to_area->fa_cur;
            rc = ffs_flash_copy(from_area_idx, object->fo_area_offset,
                                ffs_scratch_area_idx, to_offset,
                                obj_size);
            if (rc != 0) {
                return rc;
            }
            object->fo_area_idx = ffs_scratch_area_idx;
            object->fo_area_offset = to_offset;
        }
    }

    from_area->fa_gc_seq++;
    rc = ffs_format_area(from_area_idx, 1);
    if (rc != 0) {
        return rc;
    }

    if (out_area_idx != NULL) {
        *out_area_idx = ffs_scratch_area_idx;
    }

    ffs_scratch_area_idx = from_area_idx;

    return 0;
}

int
ffs_gc_until(uint16_t *out_area_idx, uint32_t space)
{
    int rc;
    int i;

    for (i = 0; i < ffs_num_areas; i++) {
        rc = ffs_gc(out_area_idx);
        if (rc != 0) {
            return rc;
        }

        if (ffs_area_free_space(ffs_areas + *out_area_idx) >= space) {
            return 0;
        }
    }

    return FFS_EFULL;
}
