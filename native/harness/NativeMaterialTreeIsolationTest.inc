// SPDX-License-Identifier: GPL-3.0-or-later
// Hold one private Main inside an update callback, then update another Main.
// A process-global recursion guard used to silently skip the second update.
{
  std::unique_ptr<Main, decltype(&BKE_main_free)> left(BKE_main_new(), BKE_main_free),
      right(BKE_main_new(), BKE_main_free);
  const auto fixture = [&](Main &owner) {
    auto *material = BKE_material_add(&owner, "Worker tree isolation");
    auto &tree = *material->nodetree;
    bke::node_add_node(nullptr, tree, UString("ShaderNodeBsdfPrincipled"));
    BKE_ntree_update_tag_all(&tree);
    return &tree;
  };
  auto *a = fixture(*left), *b = fixture(*right);
  std::promise<void> entered_promise, release_promise;
  auto entered = entered_promise.get_future();
  auto release = release_promise.get_future().share();
  std::atomic_bool notified = false;
  auto first = std::async(std::launch::async, [&] {
    NodeTreeUpdateExtraParams params;
    params.tree_changed_fn = [&](bNodeTree &, ID &) {
      if (!notified.exchange(true))
        entered_promise.set_value();
      release.wait();
    };
    BKE_ntree_update_after_single_tree_change(*left, *a, params);
  });
  if (entered.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
    release_promise.set_value();
    first.get();
    require(false, "Private tree fixture did not enter its update callback");
  }
  int callbacks = 0;
  NodeTreeUpdateExtraParams params;
  params.tree_changed_fn = [&](bNodeTree &, ID &) { ++callbacks; };
  BKE_ntree_update_after_single_tree_change(*right, *b, params);
  release_promise.set_value();
  first.get();
  const bool legacy = qEnvironmentVariableIsSet("BTS_NATIVE_TREE_LEGACY");
  std::printf("BTS_NATIVE_TREE_ISOLATION callbacks=%d expected=%d legacy=%d\n",
              callbacks,
              legacy ? 0 : 1,
              int(legacy));
  std::fflush(stdout);
  require(callbacks == (legacy ? 0 : 1),
          "Independent material worker update was silently skipped");
  tree_worker_isolation = callbacks == 1;
}
