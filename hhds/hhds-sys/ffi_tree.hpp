#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef int64_t hhds_Tree_pos;
typedef void* hhds_TreeHandle;

#ifdef __cplusplus
extern "C" {
#endif

hhds_TreeHandle tree_int_new_empty(void);
void            tree_int_delete(hhds_TreeHandle handle);

hhds_Tree_pos get_root(hhds_TreeHandle handle);
hhds_Tree_pos get_parent(hhds_TreeHandle handle, hhds_Tree_pos node_pos);
hhds_Tree_pos get_first_child(hhds_TreeHandle handle, hhds_Tree_pos parent_pos);
hhds_Tree_pos get_last_child(hhds_TreeHandle handle, hhds_Tree_pos parent_pos);
hhds_Tree_pos get_sibling_next(hhds_TreeHandle handle, hhds_Tree_pos sibling_pos);
hhds_Tree_pos get_sibling_prev(hhds_TreeHandle handle, hhds_Tree_pos sibling_pos);

hhds_Tree_pos add_root(hhds_TreeHandle handle, int32_t data_ignored);
hhds_Tree_pos add_child(hhds_TreeHandle handle, hhds_Tree_pos parent_pos, int32_t data_ignored);
hhds_Tree_pos append_sibling(hhds_TreeHandle handle, hhds_Tree_pos sibling_pos, int32_t data_ignored);
hhds_Tree_pos insert_next_sibling(hhds_TreeHandle handle, hhds_Tree_pos sibling_pos, int32_t data_ignored);

void delete_leaf(hhds_TreeHandle handle, hhds_Tree_pos leaf_pos);
void delete_subtree(hhds_TreeHandle handle, hhds_Tree_pos subtree_root_pos);
void set_subnode(hhds_TreeHandle handle, hhds_Tree_pos node_pos, hhds_Tree_pos subnode_tid);

#ifdef __cplusplus
}
#endif
