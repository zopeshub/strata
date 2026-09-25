#include "fs/file_descriptor.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <type_traits>

#include <gtest/gtest.h>

#include "support/test_util.h"

namespace strata {
namespace {

static_assert(!std::is_copy_constructible_v<FileDescriptor>);
static_assert(!std::is_copy_assignable_v<FileDescriptor>);
static_assert(std::is_nothrow_move_constructible_v<FileDescriptor>);
static_assert(std::is_nothrow_move_assignable_v<FileDescriptor>);
static_assert(sizeof(FileDescriptor) == sizeof(int), "no overhead over a raw fd");

bool IsOpen(int fd) { return ::fcntl(fd, F_GETFD) != -1; }

int OpenDevNull() {
  const int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
  EXPECT_GE(fd, 0);
  return fd;
}

TEST(FileDescriptorTest, DefaultIsEmpty) {
  FileDescriptor fd;
  EXPECT_FALSE(fd.valid());
  EXPECT_FALSE(static_cast<bool>(fd));
  EXPECT_EQ(fd.get(), -1);
}

TEST(FileDescriptorTest, ClosesOnDestruction) {
  const int raw = OpenDevNull();
  {
    FileDescriptor fd(raw);
    EXPECT_TRUE(IsOpen(raw));
  }
  EXPECT_FALSE(IsOpen(raw));
}

TEST(FileDescriptorTest, MoveConstructionTransfersOwnership) {
  const int raw = OpenDevNull();
  FileDescriptor a(raw);
  FileDescriptor b(std::move(a));
  EXPECT_FALSE(a.valid());  // NOLINT(bugprone-use-after-move): moved-from state is specified
  EXPECT_EQ(b.get(), raw);
  EXPECT_TRUE(IsOpen(raw));
}

TEST(FileDescriptorTest, MoveAssignmentClosesPreviousFd) {
  const int raw1 = OpenDevNull();
  const int raw2 = OpenDevNull();
  FileDescriptor a(raw1);
  FileDescriptor b(raw2);
  b = std::move(a);
  EXPECT_FALSE(IsOpen(raw2)) << "b's old fd must be closed";
  EXPECT_TRUE(IsOpen(raw1));
  EXPECT_EQ(b.get(), raw1);
  EXPECT_FALSE(a.valid());  // NOLINT(bugprone-use-after-move)
}

TEST(FileDescriptorTest, SelfMoveAssignmentIsSafe) {
  const int raw = OpenDevNull();
  FileDescriptor a(raw);
  FileDescriptor& alias = a;
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#endif
  a = std::move(alias);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  EXPECT_TRUE(IsOpen(raw)) << "self-move must not close the fd";
  EXPECT_EQ(a.get(), raw);
}

TEST(FileDescriptorTest, ReleaseGivesUpOwnership) {
  const int raw = OpenDevNull();
  int released = -1;
  {
    FileDescriptor fd(raw);
    released = fd.release();
    EXPECT_FALSE(fd.valid());
  }
  EXPECT_EQ(released, raw);
  EXPECT_TRUE(IsOpen(raw));
  ::close(raw);
}

TEST(FileDescriptorTest, ResetClosesOldAndAdoptsNew) {
  const int raw1 = OpenDevNull();
  const int raw2 = OpenDevNull();
  FileDescriptor fd(raw1);
  fd.reset(raw2);
  EXPECT_FALSE(IsOpen(raw1));
  EXPECT_EQ(fd.get(), raw2);
  fd.reset();
  EXPECT_FALSE(IsOpen(raw2));
  EXPECT_FALSE(fd.valid());
}

TEST(FileDescriptorTest, CloseReportsAndEmpties) {
  const int raw = OpenDevNull();
  FileDescriptor fd(raw);
  EXPECT_OK(fd.Close());
  EXPECT_FALSE(fd.valid());
  EXPECT_FALSE(IsOpen(raw));
  EXPECT_OK(fd.Close()) << "closing an empty descriptor is a no-op";
}

TEST(FileDescriptorTest, SyncFdAllMethods) {
  testing::TempDir dir;
  const auto path = dir / "f";
  FileDescriptor fd(::open(path.c_str(), O_CREAT | O_WRONLY | O_CLOEXEC, 0644));
  ASSERT_TRUE(fd.valid());
  ASSERT_EQ(::write(fd.get(), "x", 1), 1);
  for (SyncMethod m : {SyncMethod::kFsync, SyncMethod::kFdatasync, SyncMethod::kFullFsync,
                       SyncMethod::kBarrierFsync}) {
    EXPECT_OK(SyncFd(fd.get(), m, "test"));
  }
}

TEST(FileDescriptorTest, SyncFdOnBadFdFails) {
  const Status s = SyncFd(-1, SyncMethod::kFsync, "bad fd");
  EXPECT_TRUE(s.IsIOError()) << s.ToString();
  EXPECT_NE(s.message().find("bad fd"), std::string::npos) << "context belongs in the message";
}

}  // namespace
}  // namespace strata
