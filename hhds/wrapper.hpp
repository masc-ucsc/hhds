#pragma once

#include "tree.hpp"

extern "C" {

typedef void* ForestIntHandle;
typedef void* TreeIntHandle;

// --- Tree ---
TreeIntHandle tree_int_new(ForestIntHandle forest);
void tree_int_free(TreeIntHandle tree);
hhds::Tree_pos append_sibling(TreeIntHandle tree, hhds::Tree_pos sibling_id, int data);
hhds::Tree_pos add_child(TreeIntHandle tree, hhds::Tree_pos parent_index, int data);
hhds::Tree_pos add_root(TreeIntHandle tree, int data);

void delete_leaf(TreeIntHandle tree, hhds::Tree_pos leaf_index);
void delete_subtree(TreeIntHandle tree, hhds::Tree_pos subtree_root);
void add_subtree_ref(TreeIntHandle tree, hhds::Tree_pos node_pos, hhds::Tree_pos subtree_ref);

hhds::Tree_pos insert_next_sibling(TreeIntHandle tree, hhds::Tree_pos sibling_id, int data);
int get_data(TreeIntHandle tree, hhds::Tree_pos idx);
void set_data(TreeIntHandle tree, hhds::Tree_pos idx, int data);
void print_tree(TreeIntHandle tree, int deep);


// --- Forest ---
ForestIntHandle forest_int_new();
void forest_int_free(ForestIntHandle forest);
TreeIntHandle forest_int_get_tree(ForestIntHandle forest, hhds::Tree_pos tree_ref);
hhds::Tree_pos forest_int_create_tree(ForestIntHandle forest, int root_data);
void forest_int_add_ref(ForestIntHandle forest, hhds::Tree_pos ref);
void forest_int_remove_reference(ForestIntHandle forest, hhds::Tree_pos ref);
bool forest_int_delete_tree(ForestIntHandle forest, hhds::Tree_pos ref);

}


