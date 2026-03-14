#include "BackupManager.h"
#include <filesystem>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cstdio>

namespace fs = std::filesystem;
BackupManager::BackupManager(const std::string& backupDir, size_t maxBackups)
    : m_backupDir(backupDir)
    , m_maxBackups(maxBackups)
{
    EnsureBackupDirectoryExists();
}

std::string BackupManager::CreateBackup(const std::string& filePath) {
    if (!fs::exists(filePath) || !fs::is_regular_file(filePath)) {
        return "";
    }

    try {
        if (!EnsureBackupDirectoryExists()) {
            return "";
        }

        std::string backupName = GenerateBackupName(filePath);
        fs::path backupPath = fs::path(m_backupDir) / backupName;

        // Copy file to backup location
        fs::copy_file(filePath, backupPath, fs::copy_options::overwrite_existing);

        // Cleanup old backups
        CleanupOldBackups(filePath);

        return backupPath.string();
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to create backup: %s\n", e.what());
        return "";
    }
}

std::string BackupManager::CreateDirectoryBackup(const std::string& dirPath) {
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
        return "";
    }

    try {
        if (!EnsureBackupDirectoryExists()) {
            return "";
        }

        std::string backupName = GenerateBackupName(dirPath);
        fs::path backupPath = fs::path(m_backupDir) / backupName;

        // Copy directory recursively
        fs::copy(dirPath, backupPath, fs::copy_options::recursive | fs::copy_options::overwrite_existing);

        return backupPath.string();
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to create directory backup: %s\n", e.what());
        return "";
    }
}

bool BackupManager::RestoreBackup(const std::string& backupPath,
                                   const std::string& targetPath) {
    if (!fs::exists(backupPath)) {
        return false;
    }

    try {
        std::string target = targetPath;
        if (target.empty()) {
            // Extract original filename from backup
            target = ExtractOriginalName(fs::path(backupPath).filename().string());
        }

        if (fs::is_directory(backupPath)) {
            // Restore directory
            fs::copy(backupPath, target,
                    fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        } else {
            // Restore file
            fs::copy_file(backupPath, target, fs::copy_options::overwrite_existing);
        }

        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to restore backup: %s\n", e.what());
        return false;
    }
}

std::vector<std::string> BackupManager::ListBackups(const std::string& filePath) const {
    std::vector<std::string> backups;

    if (!fs::exists(m_backupDir)) {
        return backups;
    }

    try {
        fs::path originalFile(filePath);
        std::string filenamePrefix = originalFile.filename().string() + "_";

        // Find all backups matching the base name
        // Backup format: original.ext_YYYYMMDD_HHMMSS
        for (const auto& entry : fs::directory_iterator(m_backupDir)) {
            // We check for regular file OR directory, as this function is used for both.
            if (entry.is_regular_file() || entry.is_directory()) {
                std::string filename = entry.path().filename().string();

                // Check if this backup matches the original file
                if (filename.find(filenamePrefix) == 0) {
                    backups.push_back(entry.path().string());
                }
            }
        }

        // Sort by modification time (newest first)
        std::sort(backups.begin(), backups.end(),
                 [](const std::string& a, const std::string& b) {
                     return fs::last_write_time(a) > fs::last_write_time(b);
                 });
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to list backups: %s\n", e.what());
    }

    return backups;
}

void BackupManager::CleanupOldBackups(const std::string& filePath) {
    if (filePath.empty()) {
        // Cleanup all backups based on total count
        if (!fs::exists(m_backupDir)) {
            return;
        }

        try {
            std::vector<fs::path> allBackups;
            for (const auto& entry : fs::directory_iterator(m_backupDir)) {
                if (entry.is_regular_file() || entry.is_directory()) {
                    allBackups.push_back(entry.path());
                }
            }

            // Sort by modification time (oldest first)
            std::sort(allBackups.begin(), allBackups.end(),
                     [](const fs::path& a, const fs::path& b) {
                         return fs::last_write_time(a) < fs::last_write_time(b);
                     });

            // Remove oldest backups if count exceeds limit
            while (allBackups.size() > m_maxBackups * 5) { // Global limit
                try {
                    if (fs::is_directory(allBackups.front())) {
                        fs::remove_all(allBackups.front());
                    } else {
                        fs::remove(allBackups.front());
                    }
                } catch (...) {}
                allBackups.erase(allBackups.begin());
            }
        } catch (const std::exception& e) {
            fprintf(stderr, "Failed to cleanup backups: %s\n", e.what());
        }
    } else {
        // Cleanup backups for specific file
        auto backups = ListBackups(filePath);

        // Remove oldest backups if count exceeds max
        while (backups.size() > m_maxBackups) {
            try {
                fs::path toRemove(backups.back());
                if (fs::is_directory(toRemove)) {
                    fs::remove_all(toRemove);
                } else {
                    fs::remove(toRemove);
                }
            } catch (...) {}
            backups.pop_back();
        }
    }
}

size_t BackupManager::GetTotalBackupSize() const {
    size_t totalSize = 0;

    if (!fs::exists(m_backupDir)) {
        return 0;
    }

    try {
        for (const auto& entry : fs::recursive_directory_iterator(m_backupDir)) {
            if (entry.is_regular_file()) {
                totalSize += fs::file_size(entry);
            }
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to calculate backup size: %s\n", e.what());
    }

    return totalSize;
}

void BackupManager::ClearAllBackups() {
    if (!fs::exists(m_backupDir)) {
        return;
    }

    try {
        fs::remove_all(m_backupDir);
        EnsureBackupDirectoryExists();
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to clear backups: %s\n", e.what());
    }
}

bool BackupManager::IsBackupDirectoryValid() const {
    return fs::exists(m_backupDir) && fs::is_directory(m_backupDir);
}

std::string BackupManager::GenerateBackupName(const std::string& originalPath) const {
    fs::path path(originalPath);
    std::string filename = path.filename().string();
    std::string timestamp = GetTimestamp();
    return filename + "_" + timestamp;
}

std::string BackupManager::ExtractOriginalName(const std::string& backupName) const {
    fs::path backupPath(backupName);
    std::string filename = backupPath.filename().string();

    // Split filename into parts using '_' as delimiter
    std::vector<std::string> parts;
    std::stringstream ss(filename);
    std::string part;

    while (std::getline(ss, part, '_')) {
        parts.push_back(part);
    }

    return backupName;
}

bool BackupManager::EnsureBackupDirectoryExists() {
    try {
        if (!fs::exists(m_backupDir)) {
            fs::create_directories(m_backupDir);
        }
        return fs::is_directory(m_backupDir);
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to create backup directory: %s\n", e.what());
        return false;
    }
}

std::string BackupManager::GetTimestamp() const {
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()) % 1000;

    // std::localtime() returns a pointer to a single shared static buffer and
    // is therefore not thread-safe.  Use the platform-specific re-entrant
    // variant so concurrent CreateBackup calls on different threads do not
    // corrupt each other's timestamp.
    struct tm tm_result{};
#ifdef _WIN32
    localtime_s(&tm_result, &time);
#else
    localtime_r(&time, &tm_result);
#endif

    std::stringstream ss;
    ss << std::put_time(&tm_result, "%Y%m%d_%H%M%S");
    // Append milliseconds directly (no extra underscore) so the backup
    // filename still has exactly two underscores and ExtractOriginalName
    // keeps working: stem_YYYYMMDD_HHMMSSmmm.ext
    ss << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}
