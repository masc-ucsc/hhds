// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "tree.hpp"

#include <sstream>

namespace hhds {

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
    case Statement_class::End:
      return 0;

    case Statement_class::Assign:
      return 7 + te.name.size();

    case Statement_class::Open_def:
    case Statement_class::Closed_def: {
      size_t w = 4 + te.name.size();
      if (options.node_text) {
        w += 1 + options.node_text(as_class(c)).size();
      }
      return w;
    }

    case Statement_class::Open_call:
    case Statement_class::Closed_call: {
      size_t w = te.name.size();
      if (options.node_text) {
        w += 1 + options.node_text(as_class(c)).size();
      }
      return w;
    }

    default: {  // Node
      if (options.node_text) {
        auto nt = options.node_text(as_class(c));
        bool has_colon = options.show_types && std::string_view(nt) != te.name;
        if (has_colon) {
          return name_width + 3 + te.name.size();
        }
        return nt.size();
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

    if (options.node_text && te.sclass == Statement_class::Node) {
      auto nt = options.node_text(as_class(c));
      if (options.show_types && std::string_view(nt) != te.name) {
        if (nt.size() > align.name_width) {
          align.name_width = nt.size();
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

void Tree::print_node(std::ostream& os, Tree_pos node_pos, size_t depth, const PrintAlign& align, const PrintOptions& options) const {
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
    case Statement_class::Attr:
      os << "attr " << te.name;
      break;
    case Statement_class::Use:
      os << "use";
      break;
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
      if (options.node_text) {
        auto nt = options.node_text(node);
        os << ' ' << nt;
        body_w += 1 + nt.size();
      }
      break;
    }
    case Statement_class::Open_call:
    case Statement_class::Closed_call: {
      emit_pos(node_pos);
      os << " = " << te.name;
      body_w = te.name.size();
      if (options.node_text) {
        auto nt = options.node_text(node);
        os << ' ' << nt;
        body_w += 1 + nt.size();
      }
      break;
    }
    case Statement_class::End:
      os << "end";
      break;
    default: {  // Node
      emit_pos(node_pos);
      os << " = ";
      if (options.node_text) {
        auto nt = options.node_text(node);
        os << nt;
        bool has_colon = options.show_types && std::string_view(nt) != te.name;
        if (has_colon) {
          if (align.name_width > nt.size()) {
            os << std::string(align.name_width - nt.size(), ' ');
          }
          os << " : " << te.name;
          body_w = align.name_width + 3 + te.name.size();
        } else {
          body_w = nt.size();
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

}  // namespace hhds
