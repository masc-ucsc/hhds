// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <filesystem>
#include <fstream>
#include <sstream>

#include "tree.hpp"

namespace hhds {

// ── write_dump ───────────────────────────────────────────────────────────────

void Tree::write_dump_node(std::ostream& os, Tree_pos node_pos, const std::string& prefix, bool is_last,
                           const PrintOptions& options) const {
  os << prefix << (is_last ? "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 " : "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80 ");

  // type
  const auto type = get_type(node_pos);
  const auto te   = resolve_print_type(type, options);
  os << te.name;

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

  // Children
  std::vector<Tree_pos> children;
  for (auto c = get_first_child(node_pos); c != INVALID; c = get_sibling_next(c)) {
    children.push_back(c);
  }

  const std::string child_prefix = prefix + (is_last ? "    " : "\xe2\x94\x82   ");
  for (size_t i = 0; i < children.size(); ++i) {
    write_dump_node(os, children[i], child_prefix, i == children.size() - 1, options);
  }
}

void Tree::write_dump(std::ostream& os, const PrintOptions& options) const {
  auto start_pos = get_root();
  os << name_ << '\n';

  std::vector<Tree_pos> roots;
  for (auto c = start_pos; c != INVALID; c = get_sibling_next(c)) {
    roots.push_back(c);
  }

  for (size_t i = 0; i < roots.size(); ++i) {
    write_dump_node(os, roots[i], "", i == roots.size() - 1, options);
  }
}

void Tree::write_dump(const std::string& filename, const PrintOptions& options) const {
  std::ofstream ofs(filename);
  I(ofs.is_open(), "write_dump: Cannot open file for writing");
  write_dump(ofs, options);
}

// ── read_dump ────────────────────────────────────────────────────────────────

namespace {

// UTF-8 sequences for tree drawing characters
// ├── = E2 94 9C E2 94 80 E2 94 80 20
// └── = E2 94 94 E2 94 80 E2 94 80 20
// │   = E2 94 82 20 20 20
//         (4 display columns per level, but variable bytes due to UTF-8)

// Returns the depth (0-based) and the position in the string where the node content starts.
std::pair<size_t, size_t> parse_prefix(const std::string& line) {
  size_t       depth = 0;
  size_t       pos   = 0;
  const size_t len   = line.size();

  while (pos < len) {
    // Check for connector (├── or └──) — marks the node itself
    if (pos + 9 < len && (unsigned char)line[pos] == 0xe2) {
      unsigned char b1 = (unsigned char)line[pos + 1];
      unsigned char b2 = (unsigned char)line[pos + 2];
      if ((b1 == 0x94 && b2 == 0x9c) || (b1 == 0x94 && b2 == 0x94)) {
        // ├ or └ — skip "├── " or "└── " (3-byte char + "── " = 10 bytes)
        pos += 10;
        return {depth, pos};
      }
    }

    // Check for "│   " (vertical bar + 3 spaces = 3-byte char + 3 spaces = 6 bytes)
    if (pos + 5 < len && (unsigned char)line[pos] == 0xe2 && (unsigned char)line[pos + 1] == 0x94
        && (unsigned char)line[pos + 2] == 0x82) {
      pos += 6;
      depth++;
      continue;
    }

    // Check for "    " (4 spaces — from last-child parent)
    if (pos + 3 < len && line[pos] == ' ' && line[pos + 1] == ' ' && line[pos + 2] == ' ' && line[pos + 3] == ' ') {
      pos += 4;
      depth++;
      continue;
    }

    // Shouldn't reach here for well-formed dump output
    break;
  }

  return {depth, pos};
}

struct ParsedLine {
  size_t                                           depth;
  std::string                                      type_name;
  std::string                                      node_text;
  std::vector<std::pair<std::string, std::string>> attributes;
  Tree_pos                                         subnode_tid = INVALID;
};

ParsedLine parse_dump_line(const std::string& line) {
  ParsedLine result{};

  auto [depth, content_start] = parse_prefix(line);
  result.depth                = depth;

  std::string content = line.substr(content_start);
  size_t      pos     = 0;
  size_t      len     = content.size();

  // Trim trailing whitespace/newline
  while (len > 0 && (content[len - 1] == '\n' || content[len - 1] == '\r' || content[len - 1] == ' ')) {
    --len;
  }
  content.resize(len);

  // Parse type_name
  size_t type_start = pos;
  while (pos < len && content[pos] != ' ') {
    ++pos;
  }
  result.type_name = content.substr(type_start, pos - type_start);

  // Skip spaces
  while (pos < len && content[pos] == ' ') {
    ++pos;
  }

  // Check for 'node_text'
  if (pos < len && content[pos] == '\'') {
    ++pos;  // skip opening '
    size_t text_start = pos;
    while (pos < len && content[pos] != '\'') {
      ++pos;
    }
    result.node_text = content.substr(text_start, pos - text_start);
    if (pos < len) {
      ++pos;  // skip closing '
    }
    while (pos < len && content[pos] == ' ') {
      ++pos;
    }
  }

  // Check for @(...)
  if (pos + 1 < len && content[pos] == '@' && content[pos + 1] == '(') {
    pos += 2;
    size_t attrs_start = pos;
    while (pos < len && content[pos] != ')') {
      ++pos;
    }
    std::string attrs_str = content.substr(attrs_start, pos - attrs_start);
    if (pos < len) {
      ++pos;  // skip ')'
    }

    size_t apos = 0;
    while (apos < attrs_str.size()) {
      while (apos < attrs_str.size() && attrs_str[apos] == ' ') {
        ++apos;
      }
      size_t key_start = apos;
      while (apos < attrs_str.size() && attrs_str[apos] != '=') {
        ++apos;
      }
      std::string key = attrs_str.substr(key_start, apos - key_start);
      if (apos < attrs_str.size()) {
        ++apos;
      }
      size_t val_start = apos;
      while (apos < attrs_str.size() && attrs_str[apos] != ',') {
        ++apos;
      }
      std::string val = attrs_str.substr(val_start, apos - val_start);
      if (apos < attrs_str.size()) {
        ++apos;
      }
      if (!key.empty()) {
        result.attributes.emplace_back(std::move(key), std::move(val));
      }
    }

    while (pos < len && content[pos] == ' ') {
      ++pos;
    }
  }

  // Check for "subnode @N"
  if (pos + 8 < len && content.substr(pos, 8) == "subnode ") {
    pos += 8;
    if (pos < len && content[pos] == '@') {
      ++pos;
      result.subnode_tid = std::stoll(content.substr(pos));
    }
  }

  return result;
}

}  // namespace

Tree::ReadDumpResult Tree::read_dump(std::istream& is, std::span<const Type_entry> type_table) {
  // Build reverse map: type_name -> Type (index in type_table)
  std::vector<std::pair<std::string_view, Type>> type_map;
  for (size_t i = 0; i < type_table.size(); ++i) {
    type_map.emplace_back(type_table[i].name, static_cast<Type>(i));
  }

  auto find_type = [&](const std::string& name) -> Type {
    for (const auto& [tname, tid] : type_map) {
      if (tname == name) {
        return tid;
      }
    }
    return 0;  // fallback to type 0
  };

  std::string line;

  // First line: tree name
  if (!std::getline(is, line)) {
    return {Tree::create(), {}};
  }
  // Trim trailing whitespace
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
    line.pop_back();
  }

  auto tree = Tree::create();
  tree->set_name(line);

  std::vector<NodeData> nodes;
  std::vector<Tree_pos> stack;  // stack[depth] = Tree_pos of the last node at that depth

  while (std::getline(is, line)) {
    if (line.empty()) {
      continue;
    }

    auto parsed = parse_dump_line(line);

    Tree_pos new_pos;
    if (stack.empty()) {
      // First node = root
      new_pos = tree->add_root();
    } else if (parsed.depth == 0) {
      // Another root-level sibling
      new_pos = tree->append_sibling(stack[0]);
    } else {
      // Child of the node at depth-1
      size_t parent_depth = parsed.depth - 1;
      I(parent_depth < stack.size(), "read_dump: invalid depth in file");
      new_pos = tree->add_child(stack[parent_depth]);
    }

    // Set type
    tree->set_type(new_pos, find_type(parsed.type_name));

    // Set subnode if present
    if (parsed.subnode_tid != INVALID) {
      tree->set_subnode(new_pos, parsed.subnode_tid);
    }

    // Update stack
    if (parsed.depth >= stack.size()) {
      stack.resize(parsed.depth + 1, INVALID);
    }
    stack[parsed.depth] = new_pos;
    // Invalidate deeper levels (new branch)
    stack.resize(parsed.depth + 1);

    // Record node data
    NodeData nd;
    nd.pos        = new_pos;
    nd.node_text  = parsed.node_text.empty() ? parsed.type_name : parsed.node_text;
    nd.attributes = std::move(parsed.attributes);
    nodes.push_back(std::move(nd));
  }

  return {std::move(tree), std::move(nodes)};
}

Tree::ReadDumpResult Tree::read_dump(const std::string& filename, std::span<const Type_entry> type_table) {
  std::ifstream ifs(filename);
  I(ifs.is_open(), "read_dump: Cannot open file for reading");
  return read_dump(ifs, type_table);
}

// --------------------------------------------------------------------------
// Binary persistence
// --------------------------------------------------------------------------

static constexpr uint32_t TREE_BODY_MAGIC   = 0x48485442;  // "HHTB"
static constexpr uint32_t TREE_BODY_VERSION = 1;
static constexpr uint32_t ENDIAN_CHECK      = 0x01020304;

void Tree::save_body(const std::string& dir_path) const {
  namespace fs = std::filesystem;
  fs::create_directories(dir_path);

  const auto    path = fs::path(dir_path) / "body.bin";
  std::ofstream ofs(path, std::ios::binary);
  assert(ofs.good() && "save_body: cannot open body.bin for writing");

  const uint64_t pointers_count = pointers_stack.size();
  const uint64_t validity_count = validity_stack.size();
  const uint64_t subnode_count  = subnode_refs.size();

  ofs.write(reinterpret_cast<const char*>(&TREE_BODY_MAGIC), sizeof(TREE_BODY_MAGIC));
  ofs.write(reinterpret_cast<const char*>(&TREE_BODY_VERSION), sizeof(TREE_BODY_VERSION));
  ofs.write(reinterpret_cast<const char*>(&ENDIAN_CHECK), sizeof(ENDIAN_CHECK));
  ofs.write(reinterpret_cast<const char*>(&pointers_count), sizeof(pointers_count));
  ofs.write(reinterpret_cast<const char*>(&validity_count), sizeof(validity_count));
  ofs.write(reinterpret_cast<const char*>(&subnode_count), sizeof(subnode_count));

  // Bulk write — all pointer-free POD arrays.
  ofs.write(reinterpret_cast<const char*>(pointers_stack.data()),
            static_cast<std::streamsize>(pointers_count * sizeof(Tree_pointers)));
  ofs.write(reinterpret_cast<const char*>(validity_stack.data()),
            static_cast<std::streamsize>(validity_count * sizeof(std::bitset<64>)));
  ofs.write(reinterpret_cast<const char*>(subnode_refs.data()),
            static_cast<std::streamsize>(subnode_count * sizeof(Tree_pos)));
  dirty_ = false;
}

void Tree::load_body(const std::string& dir_path) {
  namespace fs = std::filesystem;

  const auto    path = fs::path(dir_path) / "body.bin";
  std::ifstream ifs(path, std::ios::binary);
  assert(ifs.good() && "load_body: cannot open body.bin for reading");

  uint32_t magic = 0, version = 0, endian = 0;
  ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  ifs.read(reinterpret_cast<char*>(&version), sizeof(version));
  ifs.read(reinterpret_cast<char*>(&endian), sizeof(endian));
  assert(magic == TREE_BODY_MAGIC && "load_body: bad magic");
  assert(version == TREE_BODY_VERSION && "load_body: unsupported version");
  assert(endian == ENDIAN_CHECK && "load_body: endian mismatch");

  uint64_t pointers_count = 0, validity_count = 0, subnode_count = 0;
  ifs.read(reinterpret_cast<char*>(&pointers_count), sizeof(pointers_count));
  ifs.read(reinterpret_cast<char*>(&validity_count), sizeof(validity_count));
  ifs.read(reinterpret_cast<char*>(&subnode_count), sizeof(subnode_count));

  pointers_stack.resize(pointers_count);
  ifs.read(reinterpret_cast<char*>(pointers_stack.data()),
           static_cast<std::streamsize>(pointers_count * sizeof(Tree_pointers)));

  validity_stack.resize(validity_count);
  ifs.read(reinterpret_cast<char*>(validity_stack.data()),
           static_cast<std::streamsize>(validity_count * sizeof(std::bitset<64>)));

  subnode_refs.resize(subnode_count);
  ifs.read(reinterpret_cast<char*>(subnode_refs.data()),
           static_cast<std::streamsize>(subnode_count * sizeof(Tree_pos)));
  dirty_ = false;
}

// --------------------------------------------------------------------------
// Forest persistence
// --------------------------------------------------------------------------

void Forest::save(const std::string& db_path) const {
  namespace fs = std::filesystem;
  fs::create_directories(db_path);

  // --- forest.txt (declarations, text format) ---
  {
    std::ofstream ofs(fs::path(db_path) / "forest.txt");
    assert(ofs.good() && "Forest::save: cannot open forest.txt");
    ofs << "hhds_forest 1\n";
    for (size_t i = 0; i < tree_ios_.size(); ++i) {
      const auto& tio = tree_ios_[i];
      if (!tio) {
        continue;
      }
      // tid = -(i+1)
      ofs << "tree_io " << i << " " << tio->get_name() << "\n";
    }
  }

  // --- tree body directories (skip clean trees) ---
  for (size_t i = 0; i < trees.size(); ++i) {
    if (!trees[i] || !trees[i]->is_dirty()) {
      continue;
    }
    const auto dir = fs::path(db_path) / ("tree_" + std::to_string(i));
    trees[i]->save_body(dir.string());
  }
}

void Forest::load(const std::string& db_path) {
  namespace fs = std::filesystem;

  // Clear current state.
  tree_ios_.clear();
  trees.clear();
  reference_counts.clear();
  tree_name_to_tid_.clear();

  // --- Parse forest.txt ---
  {
    std::ifstream ifs(fs::path(db_path) / "forest.txt");
    assert(ifs.good() && "Forest::load: cannot open forest.txt");

    std::string line;
    std::getline(ifs, line);  // header: "hhds_forest 1"

    while (std::getline(ifs, line)) {
      if (line.empty()) {
        continue;
      }
      if (line.substr(0, 8) == "tree_io ") {
        std::istringstream ss(line.substr(8));
        size_t      idx;
        std::string name;
        ss >> idx >> name;
        Tid tid = -static_cast<Tree_pos>(idx + 1);
        (void)create_io_impl(tid, name);
      }
    }
  }

  // --- Load tree bodies ---
  for (size_t i = 0; i < tree_ios_.size(); ++i) {
    const auto& tio = tree_ios_[i];
    if (!tio) {
      continue;
    }
    const auto dir = fs::path(db_path) / ("tree_" + std::to_string(i));
    if (fs::exists(dir / "body.bin")) {
      auto tree = tio->create_tree();
      tree->load_body(dir.string());
    }
  }
}

}  // namespace hhds
