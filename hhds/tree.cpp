// test.cpp
#include "hhds-sys/ffi_tree.hpp"
#include "tree.hpp"

static_assert(sizeof(hhds::Tree_pointers) == 192);  // 64B alignment keeps the struct at three cache lines

namespace {
using TreeHandle = std::shared_ptr<hhds::Tree>;

auto as_tree(hhds_TreeHandle handle) -> TreeHandle* {
  return static_cast<TreeHandle*>(handle);
}
}

extern "C" {

hhds_TreeHandle tree_int_new_empty(void) {
  return new TreeHandle(hhds::Tree::create());
}

void tree_int_delete(hhds_TreeHandle handle) {
  delete as_tree(handle);
}

hhds_Tree_pos get_root(hhds_TreeHandle handle) {
  return (*as_tree(handle))->get_root();
}

hhds_Tree_pos get_parent(hhds_TreeHandle handle, hhds_Tree_pos node_pos) {
  return (*as_tree(handle))->get_parent(node_pos);
}

hhds_Tree_pos get_first_child(hhds_TreeHandle handle, hhds_Tree_pos parent_pos) {
  return (*as_tree(handle))->get_first_child(parent_pos);
}

hhds_Tree_pos get_last_child(hhds_TreeHandle handle, hhds_Tree_pos parent_pos) {
  return (*as_tree(handle))->get_last_child(parent_pos);
}

hhds_Tree_pos get_sibling_next(hhds_TreeHandle handle, hhds_Tree_pos sibling_pos) {
  return (*as_tree(handle))->get_sibling_next(sibling_pos);
}

hhds_Tree_pos get_sibling_prev(hhds_TreeHandle handle, hhds_Tree_pos sibling_pos) {
  return (*as_tree(handle))->get_sibling_prev(sibling_pos);
}

hhds_Tree_pos add_root(hhds_TreeHandle handle, int32_t /*data_ignored*/) {
  return (*as_tree(handle))->add_root();
}

hhds_Tree_pos add_child(hhds_TreeHandle handle, hhds_Tree_pos parent_pos, int32_t /*data_ignored*/) {
  return (*as_tree(handle))->add_child(parent_pos);
}

hhds_Tree_pos append_sibling(hhds_TreeHandle handle, hhds_Tree_pos sibling_pos, int32_t /*data_ignored*/) {
  return (*as_tree(handle))->append_sibling(sibling_pos);
}

hhds_Tree_pos insert_next_sibling(hhds_TreeHandle handle, hhds_Tree_pos sibling_pos, int32_t /*data_ignored*/) {
  return (*as_tree(handle))->insert_next_sibling(sibling_pos);
}

void delete_leaf(hhds_TreeHandle handle, hhds_Tree_pos leaf_pos) {
  (*as_tree(handle))->delete_leaf(leaf_pos);
}

void delete_subtree(hhds_TreeHandle handle, hhds_Tree_pos subtree_root_pos) {
  (*as_tree(handle))->delete_subtree(subtree_root_pos);
}

void set_subnode(hhds_TreeHandle handle, hhds_Tree_pos node_pos, hhds_Tree_pos subnode_tid) {
  (*as_tree(handle))->set_subnode(node_pos, subnode_tid);
}

}
