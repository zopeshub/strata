#include "io/task.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "strata/status.h"

namespace strata {
namespace {

// The body is intentionally trivial: these tests are about the machinery.
Task<int> Value(int v) { co_return v; }

Task<int> Add(int a, int b) {
  const int x = co_await Value(a);
  const int y = co_await Value(b);
  co_return x + y;
}

TEST(TaskTest, SyncWaitReturnsValue) { EXPECT_EQ(SyncWait(Value(42)), 42); }

TEST(TaskTest, Nesting) { EXPECT_EQ(SyncWait(Add(40, 2)), 42); }

Task<int> Deep(int depth) {
  if (depth == 0) co_return 0;
  co_return 1 + co_await Deep(depth - 1);
}

TEST(TaskTest, DeepNesting) { EXPECT_EQ(SyncWait(Deep(1000)), 1000); }

TEST(TaskTest, IsLazy) {
  bool started = false;
  auto body = [](bool& flag) -> Task<int> {
    flag = true;
    co_return 1;
  };
  Task<int> t = body(started);
  EXPECT_FALSE(started) << "creating a Task must not run its body";
  EXPECT_EQ(SyncWait(std::move(t)), 1);
  EXPECT_TRUE(started);
}

TEST(TaskTest, ExecutionOrder) {
  std::vector<std::string> log;
  auto inner = [](std::vector<std::string>& l) -> Task<int> {
    l.emplace_back("inner");
    co_return 7;
  };
  auto outer = [&inner](std::vector<std::string>& l) -> Task<int> {
    l.emplace_back("outer-before");
    Task<int> child = inner(l);
    l.emplace_back("outer-created-child");
    const int v = co_await std::move(child);
    l.emplace_back("outer-after:" + std::to_string(v));
    co_return v;
  };
  EXPECT_EQ(SyncWait(outer(log)), 7);
  EXPECT_EQ(log, (std::vector<std::string>{"outer-before", "outer-created-child", "inner",
                                           "outer-after:7"}));
}

TEST(TaskTest, MoveOnlyResult) {
  auto make = []() -> Task<std::unique_ptr<int>> { co_return std::make_unique<int>(9); };
  std::unique_ptr<int> p = SyncWait(make());
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(*p, 9);
}

TEST(TaskTest, StatusAndResultBodies) {
  auto ok = []() -> Task<Status> { co_return Status::OK(); };
  auto err = []() -> Task<Result<int>> { co_return Err(Status::Corruption("bad")); };
  EXPECT_TRUE(SyncWait(ok()).ok());
  auto r = SyncWait(err());
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsCorruption());
}

TEST(TaskTest, ExceptionsPropagateThroughCoAwait) {
  auto thrower = []() -> Task<int> {
    throw std::runtime_error("boom");
    co_return 0;
  };
  auto middle = [&thrower]() -> Task<int> { co_return 1 + co_await thrower(); };
  EXPECT_THROW(SyncWait(thrower()), std::runtime_error);
  EXPECT_THROW(SyncWait(middle()), std::runtime_error);
}

struct Counted {
  explicit Counted(int& d) : dtors(&d) {}
  Counted(const Counted&) = delete;
  Counted& operator=(const Counted&) = delete;
  ~Counted() { ++*dtors; }
  int* dtors;
};

Task<int> Owns(std::unique_ptr<Counted> c) { co_return c ? 1 : 0; }

TEST(TaskTest, DestroyingANeverStartedTaskFreesTheFrame) {
  int dtors = 0;
  {
    Task<int> t = Owns(std::make_unique<Counted>(dtors));
  }
  EXPECT_EQ(dtors, 1) << "the frame (holding the parameter) must be destroyed with the Task";
}

TEST(TaskTest, CompletedTaskFreesTheFrame) {
  int dtors = 0;
  EXPECT_EQ(SyncWait(Owns(std::make_unique<Counted>(dtors))), 1);
  EXPECT_EQ(dtors, 1);
}

TEST(TaskTest, MoveSemantics) {
  static_assert(!std::is_copy_constructible_v<Task<int>>);
  static_assert(std::is_nothrow_move_constructible_v<Task<int>>);
  static_assert(std::is_nothrow_move_assignable_v<Task<int>>);
  int dtors = 0;
  Task<int> a = Owns(std::make_unique<Counted>(dtors));
  Task<int> b = std::move(a);
  EXPECT_FALSE(a.handle()) << "moved-from Task must be empty";  // NOLINT(bugprone-use-after-move)
  EXPECT_EQ(dtors, 0);
  b = Owns(std::make_unique<Counted>(dtors));
  EXPECT_EQ(dtors, 1) << "move-assignment must destroy the frame it replaces";
  Task<int>& alias = b;
  b = std::move(alias);  // self-move must be harmless
  EXPECT_EQ(SyncWait(std::move(b)), 1);
  EXPECT_EQ(dtors, 2);
}

[[maybe_unused]] Task<int> SumOfOnes(int n) {
  int sum = 0;
  for (int i = 0; i < n; i++) sum += co_await Value(1);
  co_return sum;
}

TEST(TaskTest, SymmetricTransferDoesNotGrowTheStack) {
#if defined(STRATA_ASAN)
  GTEST_SKIP() << "ASan disables the tail calls symmetric transfer relies on";
#elif defined(__GNUC__) && !defined(__clang__) && !defined(__OPTIMIZE__)
  GTEST_SKIP() << "GCC at -O0 does not guarantee the tail call; run under the release preset";
#else
  EXPECT_EQ(SyncWait(SumOfOnes(1'000'000)), 1'000'000)
      << "(a crash here = stack overflow = no symmetric transfer in final_suspend/await_suspend)";
#endif
}

TEST(TaskTest, WhenAllPreservesOrder) {
  std::vector<Task<int>> tasks;
  for (int i = 0; i < 100; i++) tasks.push_back(Add(i, i));
  std::vector<int> results = SyncWait(WhenAll(std::move(tasks)));
  ASSERT_EQ(results.size(), 100u);
  for (int i = 0; i < 100; i++) EXPECT_EQ(results[static_cast<std::size_t>(i)], 2 * i);
}

TEST(TaskTest, WhenAllOfNothing) {
  EXPECT_TRUE(SyncWait(WhenAll(std::vector<Task<int>>{})).empty());
}

TEST(TaskTest, WhenAllInsideACoroutine) {
  auto fan_out = []() -> Task<int> {
    std::vector<Task<int>> tasks;
    for (int i = 1; i <= 10; i++) tasks.push_back(Value(i));
    int sum = 0;
    for (int v : co_await WhenAll(std::move(tasks))) sum += v;
    co_return sum;
  };
  EXPECT_EQ(SyncWait(fan_out()), 55);
}

}  // namespace
}  // namespace strata
