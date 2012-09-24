#include <gtest/gtest.h>
#include <errno.h>
#include <iostream>
#include <set>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "file_db.h"
#include "filesystem_op.h"
#include "types.h"
#include "util.h"

namespace {

const unsigned kStorageDepth = 3;

class BasicFileStorageTest : public ::testing::Test {
 protected:
  BasicFileStorageTest() :
      file_db_(NULL) {}

  void SetUp() {
    file_base_ = util::CreateTemporaryDirectory("/tmp/ctlogXXXXXX");
    ASSERT_EQ("/tmp/ctlog", file_base_.substr(0, 10));
    ASSERT_EQ(16U, file_base_.length());
    file_db_ = new FileStorage(file_base_, kStorageDepth);
  }

  void TearDown() {
    // Check again that it is safe to empty file_base_.
    ASSERT_EQ("/tmp/ctlog", file_base_.substr(0, 10));
    ASSERT_EQ(16U, file_base_.length());
    std::string command = "rm -r " + file_base_;
    int ret = system(command.c_str());
    if (ret != 0)
      std::cout << "Failed to delete temporary directory in "
                << file_base_ << std::endl;
  }

  ~BasicFileStorageTest() {
    if (file_db_ != NULL)
    delete file_db_;
  }

  FileStorage *file_db_;
  std::string file_base_;
};

TEST_F(BasicFileStorageTest, Create) {
  bstring key0("1234xyzw", 8);
  bstring value0("unicorn", 7);

  bstring key1("1245abcd", 8);
  bstring value1("Alice", 5);

  EXPECT_EQ(FileStorage::NOT_FOUND, file_db_->LookupEntry(key0, NULL));
  EXPECT_EQ(FileStorage::NOT_FOUND, file_db_->LookupEntry(key1, NULL));

  EXPECT_EQ(FileStorage::OK, file_db_->CreateEntry(key0, value0));
  bstring lookup_result;
  EXPECT_EQ(FileStorage::OK, file_db_->LookupEntry(key0, &lookup_result));
  EXPECT_EQ(value0, lookup_result);

  EXPECT_EQ(FileStorage::OK, file_db_->CreateEntry(key1, value1));
  EXPECT_EQ(FileStorage::OK, file_db_->LookupEntry(key1, &lookup_result));
  EXPECT_EQ(value1, lookup_result);
}

TEST_F(BasicFileStorageTest, Scan) {
  bstring key0("1234xyzw", 8);
  bstring value0("unicorn", 7);

  bstring key1("1245abcd", 8);
  bstring value1("Alice", 5);

  EXPECT_EQ(FileStorage::OK, file_db_->CreateEntry(key0, value0));
  EXPECT_EQ(FileStorage::OK, file_db_->CreateEntry(key1, value1));

  std::set<bstring> keys;
  keys.insert(key0);
  keys.insert(key1);

  std::set<bstring> scan_keys = file_db_->Scan();
  EXPECT_EQ(keys, scan_keys);
}

TEST_F(BasicFileStorageTest, CreateDuplicate) {
  bstring key("1234xyzw", 8);
  bstring value("unicorn", 7);

  EXPECT_EQ(FileStorage::NOT_FOUND, file_db_->LookupEntry(key, NULL));
  EXPECT_EQ(FileStorage::OK, file_db_->CreateEntry(key, value));
  bstring lookup_result;
  EXPECT_EQ(FileStorage::OK, file_db_->LookupEntry(key, &lookup_result));
  EXPECT_EQ(value, lookup_result);

  // Try to log another entry with the same key.
  bstring new_value("alice", 5);
  EXPECT_EQ(FileStorage::ENTRY_ALREADY_EXISTS,
            file_db_->CreateEntry(key, new_value));
  lookup_result.clear();
  EXPECT_EQ(FileStorage::OK, file_db_->LookupEntry(key, &lookup_result));

  // Expect to receive the original entry on lookup.
  EXPECT_EQ(value, lookup_result);
}

TEST_F(BasicFileStorageTest, Update) {
  bstring key("1234xyzw", 8);
  bstring value("unicorn", 7);

  EXPECT_EQ(FileStorage::NOT_FOUND, file_db_->LookupEntry(key, NULL));
  EXPECT_EQ(FileStorage::OK, file_db_->CreateEntry(key, value));
  bstring lookup_result;
  EXPECT_EQ(FileStorage::OK, file_db_->LookupEntry(key, &lookup_result));
  EXPECT_EQ(value, lookup_result);

  // Update.
  bstring new_value("alice", 5);
  EXPECT_EQ(FileStorage::OK, file_db_->UpdateEntry(key, new_value));
  EXPECT_EQ(FileStorage::OK, file_db_->LookupEntry(key, &lookup_result));

  // Expect to receive the new entry on lookup.
  EXPECT_EQ(new_value, lookup_result);
}

// Test for non-existing keys that are similar to  existing ones.
TEST_F(BasicFileStorageTest, LookupInvalidKey) {
  bstring key("1234xyzw", 8);
  bstring value("unicorn", 7);

  bstring similar_key0("1234xyz", 7);
  bstring similar_key1("1234xyzv", 8);
  bstring similar_key2("123", 3);
  bstring empty_key;

  EXPECT_EQ(FileStorage::OK, file_db_->CreateEntry(key, value));
  EXPECT_EQ(FileStorage::OK, file_db_->LookupEntry(key, NULL));
  EXPECT_EQ(FileStorage::NOT_FOUND, file_db_->LookupEntry(similar_key0, NULL));
  EXPECT_EQ(FileStorage::NOT_FOUND, file_db_->LookupEntry(similar_key1, NULL));
  EXPECT_EQ(FileStorage::NOT_FOUND, file_db_->LookupEntry(similar_key2, NULL));
  EXPECT_EQ(FileStorage::NOT_FOUND, file_db_->LookupEntry(empty_key, NULL));
}

TEST_F(BasicFileStorageTest, Resume) {
  bstring key0("1234xyzw", 8);
  bstring value0("unicorn", 7);

  bstring key1("1245abcd", 8);
  bstring value1("Alice", 5);

  EXPECT_EQ(FileStorage::OK, file_db_->CreateEntry(key0, value0));
  EXPECT_EQ(FileStorage::OK, file_db_->CreateEntry(key1, value1));

  // A second database.
  FileStorage db2(file_base_, kStorageDepth);

  // Look up and expect to find the entries.
  bstring lookup_result;
  EXPECT_EQ(FileStorage::OK, db2.LookupEntry(key0, &lookup_result));
  EXPECT_EQ(value0, lookup_result);

  EXPECT_EQ(FileStorage::OK, db2.LookupEntry(key1, &lookup_result));
  EXPECT_EQ(value1, lookup_result);
};

TEST_F(BasicFileStorageTest, ScanOnResume) {
  bstring key0("1234xyzw", 8);
  bstring value0("unicorn", 7);

  bstring key1("1245abcd", 8);
  bstring value1("Alice", 5);

  EXPECT_EQ(FileStorage::OK, file_db_->CreateEntry(key0, value0));
  EXPECT_EQ(FileStorage::OK, file_db_->CreateEntry(key1, value1));

  // A second database.
  FileStorage db2(file_base_, kStorageDepth);

  std::set<bstring> keys;
  keys.insert(key0);
  keys.insert(key1);

  std::set<bstring> scan_keys = db2.Scan();
  EXPECT_EQ(keys, scan_keys);
}

class FailingFileStorageDeathTest : public ::testing::Test {
 protected:
  FailingFileStorageDeathTest() {}

  void SetUp() {
    file_base_ = util::CreateTemporaryDirectory("/tmp/ctlogXXXXXX");
    ASSERT_EQ("/tmp/ctlog", file_base_.substr(0, 10));
    ASSERT_EQ(16U, file_base_.length());
  }

  std::string GetTemporaryDirectory() {
    return util::CreateTemporaryDirectory(file_base_ + "/ctlogXXXXXX");
  }

  void TearDown() {
    // Check again that it is safe to empty file_base_.
    ASSERT_EQ("/tmp/ctlog", file_base_.substr(0, 10));
    ASSERT_EQ(16U, file_base_.length());
    std::string command = "rm -r " + file_base_;
    int ret = system(command.c_str());
    if (ret != 0)
      std::cout << "Failed to delete temporary directory in "
                << file_base_ << std::endl;
  }

  ~FailingFileStorageDeathTest() {}

  std::string file_base_;
};

TEST(DeathTest, SupportDeath) {
#ifndef EXPECT_DEATH
  FAIL() << "Death tests not supported on this platform.";
#endif
};

// TODO(ekasper): death tests throw the following warning
// (at least on some platforms):
//
// [WARNING] ../src/gtest-death-test.cc:789:: Death tests use fork(),
// which is unsafe particularly in a threaded context.
// For this test, Google Test couldn't detect the number of threads.
//
// Find out why.

TEST_F(FailingFileStorageDeathTest, DieOnFailedCreate) {
  // Profiling run: count file operations.
  FailingFilesystemOp *failing_file_op = new FailingFilesystemOp(-1);
  FileStorage db(GetTemporaryDirectory(), kStorageDepth, failing_file_op);

  // Count ops for constructor.
  int op_count_init = failing_file_op->OpCount();
  ASSERT_GE(op_count_init, 0);

  bstring key0("1234xyzw", 8);
  bstring value0("unicorn", 7);

  EXPECT_EQ(FileStorage::OK, db.CreateEntry(key0, value0));
  int op_count0 = failing_file_op->OpCount();
  ASSERT_GT(op_count0, op_count_init);

  bstring key1("1245abcd", 8);
  bstring value1("Alice", 5);

  EXPECT_EQ(FileStorage::OK, db.CreateEntry(key1, value1));
  int op_count1 = failing_file_op->OpCount();
  ASSERT_GT(op_count1, op_count0);

  // Real run. Repeat for each file op individually.
  for (int i = op_count_init; i < op_count0; ++i) {
    FileStorage db(GetTemporaryDirectory(), kStorageDepth,
                   new FailingFilesystemOp(i));
    EXPECT_DEATH_IF_SUPPORTED(db.CreateEntry(key0, value0), "");
  }

  for (int i = op_count0; i < op_count1; ++i) {
    FileStorage db(GetTemporaryDirectory(), kStorageDepth,
                   new FailingFilesystemOp(i));
    EXPECT_EQ(FileStorage::OK, db.CreateEntry(key0, value0));
    EXPECT_DEATH_IF_SUPPORTED(db.CreateEntry(key1, value1), "");
  }
};

TEST_F(FailingFileStorageDeathTest, DieOnFailedUpdate) {
  // Profiling run: count file operations.
  FailingFilesystemOp *failing_file_op = new FailingFilesystemOp(-1);
  FileStorage db(GetTemporaryDirectory(), kStorageDepth, failing_file_op);

  bstring key("1234xyzw", 8);
  bstring value("unicorn", 7);

  EXPECT_EQ(FileStorage::OK, db.CreateEntry(key, value));
  int op_count0 = failing_file_op->OpCount();
  ASSERT_GT(op_count0, 0);

  bstring new_value("Alice", 5);

  EXPECT_EQ(FileStorage::OK, db.UpdateEntry(key, new_value));
  int op_count1 = failing_file_op->OpCount();
  ASSERT_GT(op_count1, op_count0);

  // Real run. Repeat for each file op individually.
  for (int i = op_count0; i < op_count1; ++i) {
    FileStorage db(GetTemporaryDirectory(), kStorageDepth,
                   new FailingFilesystemOp(i));
    EXPECT_EQ(FileStorage::OK, db.CreateEntry(key, value));
    EXPECT_DEATH_IF_SUPPORTED(db.UpdateEntry(key, new_value), "");
  }
};

TEST_F(FailingFileStorageDeathTest, ResumeOnFailedCreate) {
  // Profiling run: count file operations.
  FailingFilesystemOp *failing_file_op = new FailingFilesystemOp(-1);
  FileStorage db(GetTemporaryDirectory(), kStorageDepth, failing_file_op);

  bstring key0("1234xyzw", 8);
  bstring value0("unicorn", 7);

 int op_count_init = failing_file_op->OpCount();
  EXPECT_EQ(FileStorage::OK, db.CreateEntry(key0, value0));
  int op_count0 = failing_file_op->OpCount();
  ASSERT_GT(op_count0, 0);

  bstring key1("1245abcd", 8);
  bstring value1("Alice", 5);

  EXPECT_EQ(FileStorage::OK, db.CreateEntry(key1, value1));
  int op_count1 = failing_file_op->OpCount();
  ASSERT_GT(op_count1, op_count0);

  // Real run. Repeat for each file op individually.
  for (int i = op_count_init; i < op_count0; ++i) {
    std::string db_dir = GetTemporaryDirectory();
    FileStorage db(db_dir, kStorageDepth, new FailingFilesystemOp(i));
    EXPECT_DEATH_IF_SUPPORTED(db.CreateEntry(key0, value0), "");
    FileStorage db2(db_dir, kStorageDepth);
    // Entry should not be there, and we should be able to insert it.
    EXPECT_EQ(FileStorage::NOT_FOUND, db2.LookupEntry(key0, NULL));
    EXPECT_EQ(FileStorage::OK, db2.CreateEntry(key0, value0));
    // Look it up to double-check that everything works.
    bstring lookup_result;
    EXPECT_EQ(FileStorage::OK, db2.LookupEntry(key0, &lookup_result));
    EXPECT_EQ(value0, lookup_result);
  }

  for (int i = op_count0; i < op_count1; ++i) {
    std::string db_dir = GetTemporaryDirectory();
    FileStorage db(db_dir, kStorageDepth, new FailingFilesystemOp(i));
    EXPECT_EQ(FileStorage::OK, db.CreateEntry(key0, value0));
    EXPECT_DEATH_IF_SUPPORTED(db.CreateEntry(key1, value1), "");
    FileStorage db2(db_dir, kStorageDepth);
    // First entry should be there just fine.
    bstring lookup_result;
    EXPECT_EQ(FileStorage::OK, db2.LookupEntry(key0, &lookup_result));
    EXPECT_EQ(value0, lookup_result);

    // Second entry should not be there, and we should be able to insert it.
    EXPECT_EQ(FileStorage::NOT_FOUND, db2.LookupEntry(key1, NULL));
    EXPECT_EQ(FileStorage::OK, db2.CreateEntry(key1, value1));
    // Look it up to double-check that everything works.
    EXPECT_EQ(FileStorage::OK, db2.LookupEntry(key1, &lookup_result));
    EXPECT_EQ(value1, lookup_result);
  }
}

TEST_F(FailingFileStorageDeathTest, ResumeOnFailedUpdate) {
  // Profiling run: count file operations.
  FailingFilesystemOp *failing_file_op = new FailingFilesystemOp(-1);
  FileStorage db(GetTemporaryDirectory(), kStorageDepth, failing_file_op);

  bstring key("1234xyzw", 8);
  bstring value("unicorn", 7);

  EXPECT_EQ(FileStorage::OK, db.CreateEntry(key, value));
  int op_count0 = failing_file_op->OpCount();
  ASSERT_GT(op_count0, 0);

  bstring new_value("Alice", 5);

  EXPECT_EQ(FileStorage::OK, db.UpdateEntry(key, new_value));
  int op_count1 = failing_file_op->OpCount();
  ASSERT_GT(op_count1, op_count0);

  // Real run. Repeat for each file op individually.
  for (int i = op_count0; i < op_count1; ++i) {
    std::string db_dir = GetTemporaryDirectory();
    FileStorage db(db_dir, kStorageDepth, new FailingFilesystemOp(i));
    EXPECT_EQ(FileStorage::OK, db.CreateEntry(key, value));
    EXPECT_DEATH_IF_SUPPORTED(db.UpdateEntry(key, new_value), "");
    FileStorage db2(db_dir, kStorageDepth);
    // The entry should be there just fine...
    bstring lookup_result;
    EXPECT_EQ(FileStorage::OK, db2.LookupEntry(key, &lookup_result));
    // ... but it should still have its old value.
    EXPECT_EQ(value, lookup_result);
  }
}

}  // namespace

int main(int argc, char**argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
