#include "tree.hpp"
#include "wrapper.hpp"

using ForestInt = hhds::Forest<int>;
using TreeInt = hhds::tree<int>;
// --- Tree ---

template class hhds::tree<int>; // This explicitly instantiates the template for int
template class hhds::Forest<int>; // This explicitly instantiates the template for int

TreeIntHandle tree_int_new(ForestIntHandle forest) {
    //return new TreeInt(forest);
    ForestInt* typed_forest = static_cast<ForestInt*>(forest);
    return static_cast<TreeIntHandle>(new TreeInt(typed_forest));
}

void tree_int_free(TreeIntHandle tree) {
	delete static_cast<TreeInt *>(tree);
}

hhds::Tree_pos append_sibling(TreeIntHandle tree, hhds::Tree_pos sibling_id, int data) {
	return static_cast<TreeInt *>(tree)->append_sibling(sibling_id, data);
}

//hhds::tree<int>::append_sibling(long const&, int const&)


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
int get_data(TreeIntHandle tree, hhds::Tree_pos idx) {
	return static_cast<TreeInt *>(tree)->get_data(idx);
}
void set_data(TreeIntHandle tree, hhds::Tree_pos idx, int data) {
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
	TreeInt* tree = new TreeInt(f->get_tree(tree_ref));
	return static_cast<TreeIntHandle>(tree);
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

