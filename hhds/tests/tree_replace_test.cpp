// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "hhds/tree.hpp"

namespace {

std::shared_ptr<hhds::Tree> make_staged_tree(const std::shared_ptr<hhds::Forest>& forest, std::string_view label) {
  auto staged = forest->create_tree_temp(label);
  auto root   = staged->add_root_node();
  root.attr(hhds::attrs::name).set(std::string(label) + "_root");
  auto child = root.add_child();
  child.attr(hhds::attrs::name).set(std::string(label) + "_child");
  return staged;
}

}  // namespace

TEST(TreeIOReplace, SwapsBodyKeepsTidAndName) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("slot");

  auto orig = tio->create_tree();
  (void)orig->add_root_node().add_child();
  const auto tid_before = tio->get_tid();

  auto staged = make_staged_tree(forest, "v2");

  tio->replace(staged);

  EXPECT_EQ(tio->get_tid(), tid_before) << "tid must not change across replace";
  EXPECT_EQ(tio->get_name(), "slot");

  // The slot now resolves to the new body.
  auto current = tio->get_tree();
  ASSERT_NE(current, nullptr);
  EXPECT_EQ(current->get_root_node().attr(hhds::attrs::name).get(), "v2_root");

  // Forest's name lookup follows the swap.
  EXPECT_EQ(forest->find_tree("slot"), current);

  // The new body is attached now.
  EXPECT_EQ(current->get_io(), tio);
  EXPECT_EQ(current->get_tid(), tid_before);
}

TEST(TreeIOReplace, DefaultDropsOldBody) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("slot");
  auto orig   = tio->create_tree();
  (void)orig->add_root_node();

  std::weak_ptr<hhds::Tree> orig_weak = orig;
  orig.reset();  // forest registry holds the only strong ref now

  auto staged = make_staged_tree(forest, "v2");
  tio->replace(staged);
  // Old body must have been dropped.
  EXPECT_TRUE(orig_weak.expired());

  // Nothing was kept in the previous slot.
  EXPECT_EQ(tio->get_previous(), nullptr);
}

TEST(TreeIOReplace, KeepPreviousMakesOldBodyReachable) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("slot");
  auto orig   = tio->create_tree();
  orig->add_root_node().attr(hhds::attrs::name).set("v1_root");

  std::weak_ptr<hhds::Tree> orig_weak = orig;
  orig.reset();

  auto staged_v2 = make_staged_tree(forest, "v2");
  tio->replace(staged_v2, /*keep_previous=*/true);

  ASSERT_FALSE(orig_weak.expired()) << "previous_ should keep the old body alive";
  auto kept = tio->get_previous();
  ASSERT_NE(kept, nullptr);
  EXPECT_EQ(kept.get(), orig_weak.lock().get());
  EXPECT_EQ(kept->get_root_node().attr(hhds::attrs::name).get(), "v1_root");

  // A second keep_previous=true replace overwrites previous_, so the v1 body
  // becomes unreachable and the v2 body becomes the previous.
  std::weak_ptr<hhds::Tree> v2_weak = tio->get_tree();
  kept.reset();
  auto staged_v3 = make_staged_tree(forest, "v3");
  tio->replace(staged_v3, /*keep_previous=*/true);

  EXPECT_TRUE(orig_weak.expired()) << "v1 body should be released when previous_ is overwritten";
  ASSERT_NE(tio->get_previous(), nullptr);
  EXPECT_EQ(tio->get_previous().get(), v2_weak.lock().get());

  // drop_previous frees the back-buffer.
  tio->drop_previous();
  EXPECT_EQ(tio->get_previous(), nullptr);
}

#ifdef TESTING
TEST(TreeIOReplace, RejectsAlreadyAttachedTree) {
  auto forest = hhds::Forest::create();
  auto tio_a  = forest->create_io("a");
  auto tio_b  = forest->create_io("b");
  (void)tio_a->create_tree();
  auto tree_b = tio_b->create_tree();

  // tree_b is already attached to tio_b — must be rejected.
  EXPECT_THROW(tio_a->replace(tree_b), std::runtime_error);

  // The cloned-but-attached case: clone() returns unattached, so this works.
  // Sanity-check: cloning then re-passing the same pointer twice fails the
  // second time (after replace, it is now attached to tio_a).
  auto cloned = tree_b->clone();
  tio_a->replace(cloned);
  // cloned is now bound to tio_a; trying to install it elsewhere must fail.
  EXPECT_THROW(tio_b->replace(cloned), std::runtime_error);
}
#endif

TEST(TreeIOReplace, SaveLoadOnlyPersistsCurrent) {
  namespace fs          = std::filesystem;
  const std::string dir = "/tmp/hhds_replace_persist";
  fs::remove_all(dir);

  {
    auto forest = hhds::Forest::create();
    auto tio    = forest->create_io("slot");
    (void)tio->create_tree();

    auto staged = make_staged_tree(forest, "v2");
    tio->replace(staged, /*keep_previous=*/true);
    ASSERT_NE(tio->get_previous(), nullptr);

    forest->save(dir);
  }

  auto forest2 = hhds::Forest::create();
  forest2->load(dir);
  auto tio2 = forest2->find_io("slot");
  ASSERT_NE(tio2, nullptr);
  auto loaded = tio2->get_tree();
  ASSERT_NE(loaded, nullptr);
  // Only v2 (the current body at save time) should have been written.
  EXPECT_EQ(loaded->get_root_node().attr(hhds::attrs::name).get(), "v2_root");
  // previous_ is debug-only; not restored.
  EXPECT_EQ(tio2->get_previous(), nullptr);

  fs::remove_all(dir);
}

TEST(TreeIOReplace, ExternalRefToOldBodyStaysAlive) {
  auto forest = hhds::Forest::create();
  auto tio    = forest->create_io("slot");
  auto orig   = tio->create_tree();
  orig->add_root_node().attr(hhds::attrs::name).set("v1_root");

  // External code holds a strong ref to the old body.
  std::shared_ptr<hhds::Tree> external = orig;

  auto staged = make_staged_tree(forest, "v2");
  tio->replace(staged);  // keep_previous=false drops slot's strong ref

  // External ref keeps it alive.
  ASSERT_NE(external, nullptr);
  EXPECT_EQ(external->get_root_node().attr(hhds::attrs::name).get(), "v1_root");

  // The slot now resolves to the new body, not the external one.
  EXPECT_NE(tio->get_tree(), external);
}
