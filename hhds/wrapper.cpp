#include "tree.hpp"
#include "wrapper.hpp"
#include "iassert.hpp"
//#include <stdexcept>

using ForestInt = hhds::Forest<int>;
using TreeInt = hhds::tree<int>;
using PreOrderIterInt = typename hhds::tree<int>::pre_order_iterator;
using PostOrderIterInt = typename hhds::tree<int>::post_order_iterator;
// --- Tree ---

template class hhds::tree<int>; // This explicitly instantiates the template for int
template class hhds::Forest<int>; // This explicitly instantiates the template for int

// --- Tree
TreeIntHandle tree_int_new_empty() {
    return static_cast<TreeIntHandle>(new TreeInt());
}

// TreeIntHandle tree_int_new_forest(ForestIntHandle forest) {
//     ForestInt* typed_forest = static_cast<ForestInt*>(forest);
//     return static_cast<TreeIntHandle>(new TreeInt(typed_forest));
// }
void tree_int_free(TreeIntHandle tree) {
	delete static_cast<TreeInt *>(tree);
}

// Tree Getters
hhds::Tree_pos get_parent(TreeIntHandle tree_handle, hhds::Tree_pos curr_index) {
	TreeInt* tree = static_cast<TreeInt *>(tree_handle);
	return tree->get_parent(curr_index);
}
hhds::Tree_pos get_last_child(TreeIntHandle tree_handle, hhds::Tree_pos parent_index) {
	TreeInt* tree = static_cast<TreeInt *>(tree_handle);
	return tree->get_last_child(parent_index);
}
hhds::Tree_pos get_first_child(TreeIntHandle tree_handle, hhds::Tree_pos parent_index) {
	TreeInt* tree = static_cast<TreeInt *>(tree_handle);
	return tree->get_first_child(parent_index);
}
hhds::Tree_pos get_root(TreeIntHandle tree_handle) {
	return static_cast<TreeInt *>(tree_handle)->get_root();
}

// Tree Setters
hhds::Tree_pos append_sibling(TreeIntHandle tree, hhds::Tree_pos sibling_id, int data) {
	return static_cast<TreeInt *>(tree)->append_sibling(sibling_id, data);
}
hhds::Tree_pos add_child(TreeIntHandle tree, hhds::Tree_pos parent_idx, int data) {
	return static_cast<TreeInt *>(tree)->add_child(parent_idx, data);
}
hhds::Tree_pos add_root(TreeIntHandle tree, int data) {
	return static_cast<TreeInt *>(tree)->add_root(data);
}
void delete_leaf(TreeIntHandle tree, hhds::Tree_pos leaf_index) {
	return static_cast<TreeInt *>(tree)->delete_leaf(leaf_index);
}
void delete_subtree(TreeIntHandle tree, hhds::Tree_pos subtree_root) {
	return static_cast<TreeInt *>(tree)->delete_subtree(subtree_root);
}
void add_subtree_ref(TreeIntHandle tree, hhds::Tree_pos node_pos, hhds::Tree_pos subtree_ref) {
	return static_cast<TreeInt *>(tree)->add_subtree_ref(node_pos, subtree_ref);
}
hhds::Tree_pos insert_next_sibling(TreeIntHandle tree, hhds::Tree_pos sibling_id, int data) {
	return static_cast<TreeInt *>(tree)->insert_next_sibling(sibling_id, data);
}



// Tree PreOrderIterator
PreOrdIterHandle get_pre_order_iterator(TreeIntHandle handle, hhds::Tree_pos start, bool follow_subtrees) {
	TreeInt* tree_ptr = static_cast<TreeInt *>(handle);
	return new TreeInt::pre_order_iterator(start, tree_ptr, follow_subtrees);
}

PreOrdIterHandle increment_pre_order_iterator(PreOrdIterHandle handle) {
	PreOrderIterInt* pre_iter_ptr = static_cast<PreOrderIterInt*>(handle);
	pre_iter_ptr->operator++();
	return handle;
}

hhds::Tree_pos deref_pre_order_iterator(PreOrdIterHandle handle) {
	PreOrderIterInt* pre_iter_ptr = static_cast<PreOrderIterInt *>(handle);
	return pre_iter_ptr->operator*();
}
int get_data_pre_order_iter(PreOrdIterHandle handle) {
	PreOrderIterInt* pre_iter_ptr = static_cast<PreOrderIterInt *>(handle);
	return pre_iter_ptr->get_data();
}
// Tree PostOrderIterator
PostOrdIterHandle get_post_order_iterator(TreeIntHandle handle, hhds::Tree_pos start, bool follow_subtrees) {
	TreeInt* tree_ptr = static_cast<TreeInt *>(handle);
	return new TreeInt::post_order_iterator(start, tree_ptr, follow_subtrees);
}

PostOrdIterHandle increment_post_order_iterator(PostOrdIterHandle handle) {
	PostOrderIterInt* post_iter_ptr = static_cast<PostOrderIterInt*>(handle);
	post_iter_ptr->operator++();
	return handle;
}

hhds::Tree_pos deref_post_order_iterator(PostOrdIterHandle handle) {
	PostOrderIterInt* post_iter_ptr = static_cast<PostOrderIterInt *>(handle);
	return post_iter_ptr->operator*();
}
int get_data_post_order_iter(PostOrdIterHandle handle) {
	PostOrderIterInt* post_iter_ptr = static_cast<PostOrderIterInt *>(handle);
	return post_iter_ptr->get_data();
}

// Tree Data Access
int tree_get_data(TreeIntHandle tree_handle, hhds::Tree_pos idx) {
	return static_cast<TreeInt *>(tree_handle)->get_data(idx);
}
void tree_set_data(TreeIntHandle tree, hhds::Tree_pos idx, int data) {
	return static_cast<TreeInt *>(tree)->set_data(idx, data);
}
void print_tree(TreeIntHandle tree, int deep) {
	return static_cast<TreeInt *>(tree)->print_tree(deep);
}

// --- Forest ---

ForestIntHandle forest_int_new() {
    return new ForestInt();
}

void forest_int_free(ForestIntHandle forest) {
	delete static_cast<ForestInt *>(forest);
}

TreeIntHandle forest_int_get_tree(ForestIntHandle forest, hhds::Tree_pos tree_ref) {
	ForestInt* f = static_cast<ForestInt *>(forest);
	return static_cast<TreeIntHandle>(&(f->get_tree(tree_ref)));
}

hhds::Tree_pos forest_int_create_tree(ForestIntHandle forest, int root_data) {
	return static_cast<ForestInt *>(forest)->create_tree(root_data);
}

void forest_int_add_ref(ForestIntHandle forest, hhds::Tree_pos ref) {
	return static_cast<ForestInt *>(forest)->add_reference(ref);
}

void forest_int_remove_reference(ForestIntHandle forest, hhds::Tree_pos ref) {
	static_cast<ForestInt *>(forest)->remove_reference(ref);
}

bool forest_int_delete_tree(ForestIntHandle forest, hhds::Tree_pos ref) {
	return static_cast<ForestInt *>(forest)->delete_tree(ref);
}

