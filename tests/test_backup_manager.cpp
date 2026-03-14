#include <gtest/gtest.h>
#include "Core/BackupManager.h"

#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static fs::path TempDir() {
    return fs::temp_directory_path() / "ib_test_backups";
}

static fs::path TempSrcDir() {
    return fs::temp_directory_path() / "ib_test_src";
}

static void WriteFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream f(path);
    f << content;
}

static std::string ReadFile(const fs::path& path) {
    std::ifstream f(path);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

// ─── Fixture ─────────────────────────────────────────────────────────────────

class BackupManagerTest : public ::testing::Test {
protected:
    fs::path backupDir = TempDir();
    fs::path srcDir    = TempSrcDir();

    void SetUp() override {
        fs::remove_all(backupDir);
        fs::remove_all(srcDir);
        fs::create_directories(srcDir);
    }

    void TearDown() override {
        fs::remove_all(backupDir);
        fs::remove_all(srcDir);
    }

    fs::path MakeSrcFile(const std::string& name, const std::string& content = "data") {
        auto p = srcDir / name;
        WriteFile(p, content);
        return p;
    }
};

// ═════════════════════════════════════════════════════════════════════════════
// Construction & directory management
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(BackupManagerTest, ConstructorCreatesBackupDirectory) {
    BackupManager mgr(backupDir.string());
    EXPECT_TRUE(fs::exists(backupDir));
    EXPECT_TRUE(mgr.IsBackupDirectoryValid());
}

TEST_F(BackupManagerTest, IsBackupDirectoryValidReturnsFalseWhenDeleted) {
    BackupManager mgr(backupDir.string());
    fs::remove_all(backupDir);
    EXPECT_FALSE(mgr.IsBackupDirectoryValid());
}

// ═════════════════════════════════════════════════════════════════════════════
// CreateBackup
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(BackupManagerTest, CreateBackupReturnsNonEmptyPath) {
    BackupManager mgr(backupDir.string());
    auto src = MakeSrcFile("config.json", R"({"version":1})");
    auto result = mgr.CreateBackup(src.string());
    EXPECT_FALSE(result.empty());
}

TEST_F(BackupManagerTest, CreateBackupFileExistsOnDisk) {
    BackupManager mgr(backupDir.string());
    auto src = MakeSrcFile("config.json", "hello");
    auto backup = mgr.CreateBackup(src.string());
    EXPECT_TRUE(fs::exists(backup));
}

TEST_F(BackupManagerTest, CreateBackupPreservesFileContent) {
    BackupManager mgr(backupDir.string());
    const std::string content = R"({"key":"value"})";
    auto src = MakeSrcFile("data.json", content);
    auto backup = mgr.CreateBackup(src.string());
    EXPECT_EQ(ReadFile(backup), content);
}

TEST_F(BackupManagerTest, CreateBackupReturnsEmptyForNonexistentFile) {
    BackupManager mgr(backupDir.string());
    auto result = mgr.CreateBackup("/nonexistent/path/file.json");
    EXPECT_TRUE(result.empty());
}

TEST_F(BackupManagerTest, CreateBackupReturnsEmptyForDirectory) {
    BackupManager mgr(backupDir.string());
    // Passing a directory path to CreateBackup (not CreateDirectoryBackup)
    auto result = mgr.CreateBackup(srcDir.string());
    EXPECT_TRUE(result.empty());
}

TEST_F(BackupManagerTest, CreateBackupFilenameContainsOriginalName) {
    BackupManager mgr(backupDir.string());
    auto src = MakeSrcFile("myfile.json");
    auto backup = mgr.CreateBackup(src.string());
    std::string backupName = fs::path(backup).filename().string();
    EXPECT_NE(backupName.find("myfile"), std::string::npos);
}

// ═════════════════════════════════════════════════════════════════════════════
// CreateDirectoryBackup
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(BackupManagerTest, CreateDirectoryBackupReturnsNonEmptyPath) {
    BackupManager mgr(backupDir.string());
    WriteFile(srcDir / "a.json", "{}");
    WriteFile(srcDir / "b.json", "{}");
    auto result = mgr.CreateDirectoryBackup(srcDir.string());
    EXPECT_FALSE(result.empty());
}

TEST_F(BackupManagerTest, CreateDirectoryBackupCopiesContents) {
    BackupManager mgr(backupDir.string());
    WriteFile(srcDir / "file.txt", "hello world");
    auto backup = mgr.CreateDirectoryBackup(srcDir.string());
    ASSERT_FALSE(backup.empty());
    ASSERT_TRUE(fs::exists(backup));

    bool found = false;
    for (auto& e : fs::recursive_directory_iterator(backup)) {
        if (e.path().filename() == "file.txt") {
            found = true;
            EXPECT_EQ(ReadFile(e.path()), "hello world");
        }
    }
    EXPECT_TRUE(found) << "Backed-up file.txt not found in " << backup;
}

TEST_F(BackupManagerTest, CreateDirectoryBackupReturnsEmptyForNonexistentDir) {
    BackupManager mgr(backupDir.string());
    auto result = mgr.CreateDirectoryBackup("/nonexistent_dir_xyz");
    EXPECT_TRUE(result.empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// ListBackups
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(BackupManagerTest, ListBackupsEmptyWhenNoBackups) {
    BackupManager mgr(backupDir.string());
    auto list = mgr.ListBackups((srcDir / "missing.json").string());
    EXPECT_TRUE(list.empty());
}

TEST_F(BackupManagerTest, ListBackupsReturnsOneAfterOneBackup) {
    BackupManager mgr(backupDir.string());
    auto src = MakeSrcFile("prefs.json", "v1");
    mgr.CreateBackup(src.string());
    auto list = mgr.ListBackups(src.string());
    EXPECT_EQ(list.size(), 1u);
}

TEST_F(BackupManagerTest, ListBackupsReturnsMultiple) {
    BackupManager mgr(backupDir.string(), 10);
    auto src = MakeSrcFile("prefs.json", "v1");
    mgr.CreateBackup(src.string());
    // Small sleep so timestamps differ
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    WriteFile(src, "v2");
    mgr.CreateBackup(src.string());
    auto list = mgr.ListBackups(src.string());
    EXPECT_EQ(list.size(), 2u);
}

TEST_F(BackupManagerTest, ListBackupsHandlesSameStemDifferentExtensions) {
    BackupManager mgr(backupDir.string());
    auto srcJson = MakeSrcFile("data.json", "json data");
    auto srcTxt  = MakeSrcFile("data.txt",  "txt data");

    mgr.CreateBackup(srcJson.string());
    mgr.CreateBackup(srcTxt.string());

    auto listJson = mgr.ListBackups(srcJson.string());
    auto listTxt  = mgr.ListBackups(srcTxt.string());

    EXPECT_EQ(listJson.size(), 1u);
    EXPECT_EQ(listTxt.size(), 1u);
}


TEST_F(BackupManagerTest, ListBackupsDoesNotIncludeOtherFiles) {
    BackupManager mgr(backupDir.string());
    auto srcA = MakeSrcFile("alpha.json", "a");
    auto srcB = MakeSrcFile("beta.json", "b");
    mgr.CreateBackup(srcA.string());
    mgr.CreateBackup(srcB.string());
    auto listA = mgr.ListBackups(srcA.string());
    // Should only see alpha backups, not beta
    for (auto& p : listA) {
        std::string name = fs::path(p).filename().string();
        EXPECT_NE(name.find("alpha"), std::string::npos) << "Unexpected file: " << name;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// RestoreBackup
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(BackupManagerTest, RestoreBackupToExplicitTarget) {
    BackupManager mgr(backupDir.string());
    const std::string original = R"({"restored":true})";
    auto src = MakeSrcFile("settings.json", original);
    auto backup = mgr.CreateBackup(src.string());

    // Overwrite source, then restore
    WriteFile(src, "corrupted");
    auto target = srcDir / "restored.json";
    bool ok = mgr.RestoreBackup(backup, target.string());

    EXPECT_TRUE(ok);
    EXPECT_EQ(ReadFile(target), original);
}

TEST_F(BackupManagerTest, RestoreBackupReturnsFalseForNonexistentBackup) {
    BackupManager mgr(backupDir.string());
    EXPECT_FALSE(mgr.RestoreBackup("/no/such/backup.json", (srcDir / "out.json").string()));
}

// ═════════════════════════════════════════════════════════════════════════════
// CleanupOldBackups
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(BackupManagerTest, CleanupKeepsAtMostMaxBackups) {
    const size_t maxBackups = 3;
    BackupManager mgr(backupDir.string(), maxBackups);
    auto src = MakeSrcFile("log.json", "v0");

    for (int i = 1; i <= 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        WriteFile(src, "v" + std::to_string(i));
        mgr.CreateBackup(src.string());
    }

    auto list = mgr.ListBackups(src.string());
    EXPECT_LE(list.size(), maxBackups);
}

// ═════════════════════════════════════════════════════════════════════════════
// GetTotalBackupSize
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(BackupManagerTest, TotalBackupSizeZeroWhenEmpty) {
    BackupManager mgr(backupDir.string());
    EXPECT_EQ(mgr.GetTotalBackupSize(), 0u);
}

TEST_F(BackupManagerTest, TotalBackupSizeNonZeroAfterBackup) {
    BackupManager mgr(backupDir.string());
    auto src = MakeSrcFile("big.json", std::string(1024, 'x'));
    mgr.CreateBackup(src.string());
    EXPECT_GT(mgr.GetTotalBackupSize(), 0u);
}

// ═════════════════════════════════════════════════════════════════════════════
// ClearAllBackups
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(BackupManagerTest, ClearAllBackupsRemovesAllFiles) {
    BackupManager mgr(backupDir.string());
    auto src = MakeSrcFile("x.json", "data");
    mgr.CreateBackup(src.string());
    ASSERT_GT(mgr.GetTotalBackupSize(), 0u);
    mgr.ClearAllBackups();
    EXPECT_EQ(mgr.GetTotalBackupSize(), 0u);
}

TEST_F(BackupManagerTest, ClearAllBackupsLeavesDirectoryValid) {
    BackupManager mgr(backupDir.string());
    auto src = MakeSrcFile("x.json", "data");
    mgr.CreateBackup(src.string());
    mgr.ClearAllBackups();
    EXPECT_TRUE(mgr.IsBackupDirectoryValid());
}
