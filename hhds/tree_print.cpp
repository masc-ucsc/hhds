// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <sstream>

#include "tree.hpp"

namespace hhds {

std::optional<std::string> Tree::node_text_override(Tree_pos node_pos, const PrintOptions& options) const {
  const auto node = as_class(node_pos);
  if (options.node_text) {
    return options.node_text(node);
  }
  auto name_attr = node.attr(attrs::name);
  if (name_attr.has()) {
    return std::string(name_attr.get());
  }
  return std::nullopt;
}

Type_entry Tree::resolve_print_type(Type type, const PrintOptions& options) const {
  if (type >= 0 && static_cast<size_t>(type) < options.type_table.size()) {
    return options.type_table[static_cast<size_t>(type)];
  }

  thread_local std::string fallback_name;
  fallback_name = "type(" + std::to_string(type) + ")";
  return Type_entry{fallback_name, Statement_class::Node};
}

size_t Tree::node_body_width(Tree_pos c, size_t name_width, const PrintOptions& options) const {
  const auto te = resolve_print_type(get_type(c), options);

  switch (te.sclass) {
    case Statement_class::Attr:
    case Statement_class::Use:
    case Statement_class::End: return 0;

    case Statement_class::Assign: return 7 + te.name.size();

    case Statement_class::Open_def:
    case Statement_class::Closed_def: {
      size_t w = 4 + te.name.size();
      if (auto nt = node_text_override(c, options); nt.has_value()) {
        w += 1 + nt->size();
      }
      return w;
    }

    case Statement_class::Open_call:
    case Statement_class::Closed_call: {
      size_t w = te.name.size();
      if (auto nt = node_text_override(c, options); nt.has_value()) {
        w += 1 + nt->size();
      }
      return w;
    }

    default: {  // Node
      if (auto nt = node_text_override(c, options); nt.has_value()) {
        const bool has_colon = options.show_types && std::string_view(*nt) != te.name;
        if (has_colon) {
          return name_width + 3 + te.name.size();
        }
        return nt->size();
      }
      return te.name.size();
    }
  }
}

// Recursively scan siblings and their non-scope flattened descendants
// to compute alignment for the visual group.
void Tree::scan_align_group(PrintAlign& align, Tree_pos first_child, const PrintOptions& options) const {
  for (auto c = first_child; c != INVALID; c = get_sibling_next(c)) {
    auto pw = std::to_string(c).size();
    if (pw > align.pos_width) {
      align.pos_width = pw;
    }

    const auto te       = resolve_print_type(get_type(c), options);
    const bool is_scope = te.sclass == Statement_class::Open_call || te.sclass == Statement_class::Closed_call
                          || te.sclass == Statement_class::Open_def || te.sclass == Statement_class::Closed_def;

    if (te.sclass == Statement_class::Node) {
      if (auto nt = node_text_override(c, options); nt.has_value() && options.show_types && std::string_view(*nt) != te.name) {
        if (nt->size() > align.name_width) {
          align.name_width = nt->size();
        }
      }
    }

    // Non-scope children print at the same indent level, include them
    if (!is_scope) {
      auto fc = get_first_child(c);
      if (fc != INVALID) {
        scan_align_group(align, fc, options);
      }
    }
  }
}

Tree::PrintAlign Tree::compute_sibling_align(Tree_pos first_child, const PrintOptions& options) const {
  PrintAlign align;
  scan_align_group(align, first_child, options);

  // body_width depends on name_width, so compute after full scan
  recompute_body_width(align, first_child, options);
  return align;
}

void Tree::recompute_body_width(PrintAlign& align, Tree_pos first_child, const PrintOptions& options) const {
  align.body_width = 0;
  recompute_body_width_recurse(align, first_child, options);
}

void Tree::recompute_body_width_recurse(PrintAlign& align, Tree_pos first_child, const PrintOptions& options) const {
  for (auto c = first_child; c != INVALID; c = get_sibling_next(c)) {
    auto bw = node_body_width(c, align.name_width, options);
    if (bw > align.body_width) {
      align.body_width = bw;
    }

    const auto te       = resolve_print_type(get_type(c), options);
    const bool is_scope = te.sclass == Statement_class::Open_call || te.sclass == Statement_class::Closed_call
                          || te.sclass == Statement_class::Open_def || te.sclass == Statement_class::Closed_def;
    if (!is_scope) {
      auto fc = get_first_child(c);
      if (fc != INVALID) {
        recompute_body_width_recurse(align, fc, options);
      }
    }
  }
}

// PrintContext implementations
void Tree::PrintContext::emit_indent(std::ostream& os) const { emit_indent(os, depth); }

void Tree::PrintContext::emit_indent(std::ostream& os, size_t d) const {
  for (size_t i = 0; i < d; ++i) {
    os << options.indent;
  }
}

std::vector<Tree_pos> Tree::PrintContext::get_children() const {
  std::vector<Tree_pos> result;
  for (auto c = tree.get_first_child(node_pos); c != INVALID; c = tree.get_sibling_next(c)) {
    result.push_back(c);
  }
  return result;
}

void Tree::PrintContext::print_children_default(std::ostream& os) const {
  auto first_child = tree.get_first_child(node_pos);
  if (first_child == INVALID) {
    return;
  }
  auto child_align = tree.compute_sibling_align(first_child, options);
  for (auto child = first_child; child != INVALID; child = tree.get_sibling_next(child)) {
    tree.print_node(os, child, depth + 1, child_align, options);
  }
}

void Tree::PrintContext::print_child_default(std::ostream& os, Tree_pos child_pos) const {
  auto       child_first = tree.get_first_child(child_pos);
  PrintAlign child_align;
  if (child_first != INVALID) {
    child_align = tree.compute_sibling_align(child_first, options);
  }
  // Compute a minimal align for the single child
  PrintAlign single_align;
  single_align.pos_width  = std::to_string(child_pos).size();
  single_align.body_width = tree.node_body_width(child_pos, 0, options);
  tree.print_node(os, child_pos, depth + 1, single_align, options);
}

void Tree::print_node(std::ostream& os, Tree_pos node_pos, size_t depth, const PrintAlign& align,
                      const PrintOptions& options) const {
  if (options.format_node) {
    PrintContext ctx{*this, options, depth, node_pos};
    if (options.format_node(os, node_pos, ctx)) {
      return;
    }
  }

  const auto node     = as_class(node_pos);
  const auto type     = get_type(node_pos);
  const auto te       = resolve_print_type(type, options);
  const bool is_scope = te.sclass == Statement_class::Open_call || te.sclass == Statement_class::Closed_call
                        || te.sclass == Statement_class::Open_def || te.sclass == Statement_class::Closed_def;

  auto emit_indent = [&](size_t d) {
    for (size_t i = 0; i < d; ++i) {
      os << options.indent;
    }
  };

  auto emit_pos = [&](Tree_pos pos) {
    auto pos_str = std::to_string(pos);
    os << '%' << pos_str;
    if (align.pos_width > pos_str.size()) {
      os << std::string(align.pos_width - pos_str.size(), ' ');
    }
  };

  auto emit_pad = [&](size_t cur_body_width) {
    if (align.body_width > cur_body_width) {
      os << std::string(align.body_width - cur_body_width, ' ');
    }
  };

  // Build attributes string @(k=v, k=v)
  std::string attr_str;
  if (!options.attributes.empty()) {
    std::string inner;
    for (const auto& [key, fn] : options.attributes) {
      auto val = fn(node);
      if (val.has_value()) {
        if (!inner.empty()) {
          inner += ", ";
        }
        inner += key + "=" + *val;
      }
    }
    if (!inner.empty()) {
      attr_str = " @(" + inner + ")";
    }
  }

  // Subnode info
  std::string subnode_str;
  if (options.show_subnodes) {
    const auto subnode_tid = get_subnode(node_pos);
    if (subnode_tid != INVALID) {
      subnode_str = " subnode @" + std::to_string(subnode_tid);
    }
  }

  emit_indent(depth);

  size_t body_w = 0;
  switch (te.sclass) {
    case Statement_class::Attr: os << "attr " << te.name; break;
    case Statement_class::Use: os << "use"; break;
    case Statement_class::Assign:
      emit_pos(node_pos);
      os << " = assign " << te.name;
      body_w = 7 + te.name.size();
      break;
    case Statement_class::Open_def:
    case Statement_class::Closed_def: {
      emit_pos(node_pos);
      os << " = def " << te.name;
      body_w = 4 + te.name.size();
      if (auto nt = node_text_override(node_pos, options); nt.has_value()) {
        os << ' ' << *nt;
        body_w += 1 + nt->size();
      }
      break;
    }
    case Statement_class::Open_call:
    case Statement_class::Closed_call: {
      emit_pos(node_pos);
      os << " = " << te.name;
      body_w = te.name.size();
      if (auto nt = node_text_override(node_pos, options); nt.has_value()) {
        os << ' ' << *nt;
        body_w += 1 + nt->size();
      }
      break;
    }
    case Statement_class::End: os << "end"; break;
    default: {  // Node
      emit_pos(node_pos);
      os << " = ";
      if (auto nt = node_text_override(node_pos, options); nt.has_value()) {
        os << *nt;
        const bool has_colon = options.show_types && std::string_view(*nt) != te.name;
        if (has_colon) {
          if (align.name_width > nt->size()) {
            os << std::string(align.name_width - nt->size(), ' ');
          }
          os << " : " << te.name;
          body_w = align.name_width + 3 + te.name.size();
        } else {
          body_w = nt->size();
        }
      } else {
        os << te.name;
        body_w = te.name.size();
      }
      break;
    }
  }

  // Pad body to align @(
  if (!attr_str.empty() || !subnode_str.empty()) {
    emit_pad(body_w);
  }
  os << attr_str << subnode_str;

  auto first_child = get_first_child(node_pos);
  if (is_scope) {
    os << " {\n";
    auto child_align = compute_sibling_align(first_child, options);
    for (auto child = first_child; child != INVALID; child = get_sibling_next(child)) {
      print_node(os, child, depth + 1, child_align, options);
    }
    emit_indent(depth);
    os << "}\n";
  } else {
    os << '\n';
    if (first_child != INVALID) {
      // Children already included in parent's alignment group
      for (auto child = first_child; child != INVALID; child = get_sibling_next(child)) {
        print_node(os, child, depth, align, options);
      }
    }
  }
}

void Tree::print(std::ostream& os, Tree_pos start_pos, const PrintOptions& options) const {
  I(_check_idx_exists(start_pos), "print: Start index out of range");
  os << name_ << " {\n";
  // Root + its non-scope descendants form one alignment group
  auto align = compute_sibling_align(start_pos, options);
  print_node(os, start_pos, 1, align, options);
  os << "}\n";
}

std::string Tree::print(const PrintOptions& options) const {
  std::ostringstream oss;
  print(oss, options);
  return oss.str();
}

std::string Tree::print(Tree_pos start_pos, const PrintOptions& options) const {
  std::ostringstream oss;
  print(oss, start_pos, options);
  return oss.str();
}

void Tree::dump_node(std::ostream& os, Tree_pos node_pos, const std::string& prefix, bool is_last,
                     const PrintOptions& options) const {
  os << prefix << (is_last ? "└── " : "├── ");

  // pos
  os << '%' << node_pos;

  // type
  const auto type = get_type(node_pos);
  const auto te   = resolve_print_type(type, options);
  os << ' ' << te.name;

  // node_text (if different from type name)
  if (options.node_text) {
    auto nt = options.node_text(as_class(node_pos));
    if (std::string_view(nt) != te.name) {
      os << " '" << nt << "'";
    }
  }

  // attributes
  if (!options.attributes.empty()) {
    std::string inner;
    for (const auto& [key, fn] : options.attributes) {
      auto val = fn(as_class(node_pos));
      if (val.has_value()) {
        if (!inner.empty()) {
          inner += ", ";
        }
        inner += key + "=" + *val;
      }
    }
    if (!inner.empty()) {
      os << " @(" << inner << ")";
    }
  }

  // subnode
  if (options.show_subnodes) {
    const auto subnode_tid = get_subnode(node_pos);
    if (subnode_tid != INVALID) {
      os << " subnode @" << subnode_tid;
    }
  }

  os << '\n';

  // Collect children
  std::vector<Tree_pos> children;
  for (auto c = get_first_child(node_pos); c != INVALID; c = get_sibling_next(c)) {
    children.push_back(c);
  }

  const std::string child_prefix = prefix + (is_last ? "    " : "│   ");
  for (size_t i = 0; i < children.size(); ++i) {
    dump_node(os, children[i], child_prefix, i == children.size() - 1, options);
  }
}

void Tree::dump(std::ostream& os, Tree_pos start_pos, const PrintOptions& options) const {
  I(_check_idx_exists(start_pos), "dump: Start index out of range");
  os << name_ << '\n';

  // Collect top-level siblings starting from start_pos
  std::vector<Tree_pos> roots;
  for (auto c = start_pos; c != INVALID; c = get_sibling_next(c)) {
    roots.push_back(c);
  }

  for (size_t i = 0; i < roots.size(); ++i) {
    dump_node(os, roots[i], "", i == roots.size() - 1, options);
  }
}

std::string Tree::dump(const PrintOptions& options) const {
  std::ostringstream oss;
  dump(oss, options);
  return oss.str();
}

std::string Tree::dump(Tree_pos start_pos, const PrintOptions& options) const {
  std::ostringstream oss;
  dump(oss, start_pos, options);
  return oss.str();
}

}  // namespace hhds
