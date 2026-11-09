#include "DocumentEditCommit.hh"
#include <iostream>
#include <string>
#include <vector>
namespace {
bool fail_allocate = false;
std::size_t allocations = 0, largest_allowed = std::numeric_limits<std::size_t>::max();
std::uint64_t ram_bytes = std::numeric_limits<std::uint64_t>::max();
std::uint64_t available_ram()
{
  return ram_bytes;
}
template<class T> struct Allocator {
  using value_type = T;
  Allocator() = default;
  template<class U> Allocator(const Allocator<U> &) {}
  T *allocate(std::size_t count)
  {
    ++allocations;
    if (fail_allocate || count > largest_allowed)
      throw std::bad_alloc();
    return std::allocator<T>{}.allocate(count);
  }
  void deallocate(T *p, std::size_t count)
  {
    std::allocator<T>{}.deallocate(p, count);
  }
  template<class U> bool operator==(const Allocator<U> &) const
  {
    return true;
  }
};
struct Document {
  int value;
};
struct Entry {
  std::unique_ptr<Document> state;
  std::uint32_t uid;
  std::string label = "Prepared edit";
};
using History = std::vector<Entry, Allocator<Entry>>;
void require(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
}  // namespace
int main()
{
  try {
    auto document = std::make_unique<Document>(Document{1}),
         candidate = std::make_unique<Document>(Document{2});
    auto *original = document.get();
    History undo, redo;
    int catalog = 1, prepared_catalog = 2, queued = 0, published = 0;
    fail_allocate = true;
    bool failed = false;
    try {
      auto entry = bts::prepare_document_commit(undo, 4);
      (void)entry;
    }
    catch (const std::bad_alloc &) {
      failed = true;
    }
    fail_allocate = false;
    require(failed && document.get() == original && document->value == 1 && undo.empty() &&
                catalog == 1,
            "Undo allocation failure moved/lost document or catalog");
    failed = false;
    try {
      auto entry = bts::prepare_document_commit(undo, 4);
      ++queued;
      throw std::runtime_error("Injected queue preparation failure");
      bts::publish_document_commit(
          document, candidate, undo, redo, std::move(entry), [&]() noexcept {
            std::swap(catalog, prepared_catalog);
            ++published;
          });
    }
    catch (const std::runtime_error &) {
      failed = true;
    }
    require(failed && queued == 1 && published == 0 && undo.empty() &&
                document.get() == original && catalog == 1,
            "Prepublication failure mutated document/history/catalog");
    redo.push_back({std::make_unique<Document>(Document{0}), 4});
    auto entry = bts::prepare_document_commit(undo, 4);
    bts::publish_document_commit(
        document, candidate, undo, redo, std::move(entry), [&]() noexcept {
          std::swap(catalog, prepared_catalog);
          ++published;
        });
    require(document->value == 2 && !candidate && undo.size() == 1 &&
                undo.back().state.get() == original && redo.empty() && catalog == 2 &&
                published == 1,
            "Successful edit was not one atomic document/catalog/Undo commit");
    History growing, empty;
    auto live = std::make_unique<Document>(Document{0});
    allocations = 0;
    for (int i = 1; i <= 64; ++i) {
      auto next = std::make_unique<Document>(Document{i});
      auto prepared = bts::prepare_document_commit(growing, 4);
      bts::publish_document_commit(
          live, next, growing, empty, std::move(prepared), []() noexcept {});
      require(live->value == i && growing.back().state->value == i - 1,
              "Geometric growth lost document history");
    }
    require(allocations <= 7 && growing.size() == 64,
            "Undo reserve defeated geometric capacity growth");
    History constrained;
    constrained.reserve(4);
    for (int i = 0; i < 4; ++i)
      constrained.push_back({std::make_unique<Document>(Document{i}), 4});
    auto *saved = constrained.front().state.get();
    {
      bts::TextureRamQueryScope query(available_ram);
      ram_bytes = 5 * sizeof(Entry);
      auto prepared = bts::prepare_document_commit(constrained, 4);
      require(constrained.capacity() == 5 && constrained.size() == 4 &&
                  constrained.front().state.get() == saved,
              "Spare-capacity RAM refusal lost history instead of exact fallback");
    }
    ram_bytes = std::numeric_limits<std::uint64_t>::max();
    largest_allowed = 6;
    auto prepared = bts::prepare_document_commit(constrained, 4);
    require(constrained.size() == 4, "Prepared metadata unexpectedly changed history");
    // Force the next growth boundary: optional 10 entries is rejected by the
    // allocator while required six entries still fit, without losing old entries.
    constrained.push_back({std::make_unique<Document>(Document{4}), 4});
    prepared = bts::prepare_document_commit(constrained, 4);
    require(constrained.capacity() == 6 && constrained.size() == 5 &&
                constrained.front().state.get() == saved,
            "Allocator growth refusal lost exact-fit history");
    largest_allowed = std::numeric_limits<std::size_t>::max();
    std::cout << "DOCUMENT_EDIT_COMMIT_PASS growth_allocations=" << allocations << "\n";
    return 0;
  }
  catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
