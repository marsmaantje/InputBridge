#pragma once

#include <string>
#include <filesystem>
#include <chrono>
#include <vector>

namespace fs = std::filesystem;

/**
 * Manages automatic backups of files before destructive operations.
 * 
 * Features:
 * - Automatic backup creation with timestamps
 * - Configurable maximum backup count
 * - Automatic cleanup of old backups
 * - Backup restoration
 * 
 * Uses RAII principles for resource management.
 */
class BackupManager {
public:
    /**
     * Create backup manager with specified backup directory.
     * 
     * @param backupDir Directory for storing backups (default: "./backups")
     * @param maxBackups Maximum number of backups to keep per file (default: 10)
     */
    explicit BackupManager(const std::string& backupDir = "./backups", 
                          size_t maxBackups = 10);
    
    /**
     * Create a backup of a file.
     * 
     * @param filePath Path to file to backup
     * @return Path to created backup file, or empty string on failure
     */
    std::string CreateBackup(const std::string& filePath);
    
    /**
     * Create a backup of a directory (recursive).
     * 
     * @param dirPath Path to directory to backup
     * @return Path to created backup directory, or empty string on failure
     */
    std::string CreateDirectoryBackup(const std::string& dirPath);
    
    /**
     * Restore a file from backup.
     * 
     * @param backupPath Path to backup file
     * @param targetPath Target path for restoration (optional, uses original if empty)
     * @return true if restoration succeeded
     */
    bool RestoreBackup(const std::string& backupPath, 
                      const std::string& targetPath = "");
    
    /**
     * List all backups for a specific file.
     * 
     * @param filePath Path to file
     * @return Vector of backup paths, sorted by creation time (newest first)
     */
    std::vector<std::string> ListBackups(const std::string& filePath) const;
    
    /**
     * Delete old backups exceeding max count.
     * 
     * @param filePath Path to file (empty for all files)
     */
    void CleanupOldBackups(const std::string& filePath = "");
    
    /**
     * Get total size of all backups in bytes.
     */
    size_t GetTotalBackupSize() const;
    
    /**
     * Delete all backups.
     */
    void ClearAllBackups();
    
    /**
     * Check if backup directory exists and is accessible.
     */
    bool IsBackupDirectoryValid() const;

private:
    std::string m_backupDir;
    size_t m_maxBackups;
    
    /**
     * Generate backup filename with timestamp.
     */
    std::string GenerateBackupName(const std::string& originalPath) const;

    /**
     * Extract original filename from backup name.
     */
    std::string ExtractOriginalName(const std::string& backupName) const;

    /**
     * Ensure backup directory exists.
     */
    bool EnsureBackupDirectoryExists();
    
    /**
     * Get timestamp string for backup naming.
     */
    std::string GetTimestamp() const;

};
