#pragma once

#include "tree.hpp"

extern "C" {

typedef void* ForestIntHandle;
typedef void* TreeIntHandle;
typedef void* SiblingOrderIterHandle;
typedef void* PreOrdIterHandle;
typedef void* PostOrdIterHandle;


// --- Tree ---
TreeIntHandle tree_int_new_empty();
void tree_int_free(TreeIntHandle tree);

// Getters
hhds::Tree_pos get_parent(TreeIntHandle tree, hhds::Tree_pos cur_index);
hhds::Tree_pos get_last_child(hhds::Tree_pos parent_index);
hhds::Tree_pos get_first_child(TreeIntHandle tree, hhds::Tree_pos parent_index);
hhds::Tree_pos get_root(TreeIntHandle tree);

// Setters 
hhds::Tree_pos append_sibling(TreeIntHandle tree, hhds::Tree_pos sibling_id, int data);
hhds::Tree_pos add_child(TreeIntHandle tree, hhds::Tree_pos parent_index, int data);
hhds::Tree_pos add_root(TreeIntHandle tree, int data);

void delete_leaf(TreeIntHandle tree, hhds::Tree_pos leaf_index);
void delete_subtree(TreeIntHandle tree, hhds::Tree_pos subtree_root);
void add_subtree_ref(TreeIntHandle tree, hhds::Tree_pos node_pos, hhds::Tree_pos subtree_ref);

hhds::Tree_pos insert_next_sibling(TreeIntHandle tree, hhds::Tree_pos sibling_id, int data);
int tree_get_data(TreeIntHandle tree, hhds::Tree_pos idx);
void tree_set_data(TreeIntHandle tree, hhds::Tree_pos idx, int data);
void print_tree(TreeIntHandle tree, int deep);

//Tree Sibling Iterator TODO: Not implemented
using SiblingOrderIter = hhds::tree<int>::sibling_order_iterator;
void *get_sibling_order_iterator(TreeIntHandle handle);
bool sibling_iterator_equal(void * iter1, void* iter2);
hhds::Tree_pos sibling_iterator_deref(void* iter);

//Tree PreOrder Iterator
using PreOrderIter = hhds::tree<int>::pre_order_iterator;
PreOrdIterHandle get_pre_order_iterator(TreeIntHandle handle, hhds::Tree_pos start, bool follow_subtrees);
PreOrdIterHandle increment_pre_order_iterator(PreOrdIterHandle handle);
hhds::Tree_pos deref_pre_order_iterator(PreOrdIterHandle handle);
int get_data_pre_order_iter(PreOrdIterHandle handle);

//Tree PostOrder Iterator
using PostOrderIter = hhds::tree<int>::post_order_iterator;
PreOrdIterHandle get_post_order_iterator(TreeIntHandle handle, hhds::Tree_pos start, bool follow_subtrees);
PreOrdIterHandle increment_post_order_iterator(PreOrdIterHandle handle);
hhds::Tree_pos deref_post_order_iterator(PreOrdIterHandle handle);
int get_data_post_order_iter(PreOrdIterHandle handle);



// --- Forest ---
ForestIntHandle forest_int_new();
void forest_int_free(ForestIntHandle forest);
TreeIntHandle forest_int_get_tree(ForestIntHandle forest, hhds::Tree_pos tree_ref);
hhds::Tree_pos forest_int_create_tree(ForestIntHandle forest, int root_data);
void forest_int_add_ref(ForestIntHandle forest, hhds::Tree_pos ref);
void forest_int_remove_reference(ForestIntHandle forest, hhds::Tree_pos ref);
bool forest_int_delete_tree(ForestIntHandle forest, hhds::Tree_pos ref);
}


