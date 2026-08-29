#pragma once

#include <IFileWatcher.h>

#include <cstdint>
#include <sys/inotify.h>

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace RSCGroup {

class InotifyFileWatcher final : public IFileWatcher {
public:
    InotifyFileWatcher();
    ~InotifyFileWatcher() override;

    InotifyFileWatcher(const InotifyFileWatcher&) = delete;
    InotifyFileWatcher& operator=(const InotifyFileWatcher&) = delete;

    void watchPath(const std::string& path) override;
    [[nodiscard]] int getPollFd() const override { return inotifyFd_; }
    void maintain() override;
    [[nodiscard]] std::vector<std::string> consumeChangedPaths() override;

private:
    [[nodiscard]] static std::string normalizePath(const std::string& path);
    void rearmDirectoryWatch(const std::string& dir);
    void retryMissingDirectoryWatches();
    void markAllWatchedPathsUnderDirChanged(const std::string& dir, std::set<std::string>& changed) const;

private:
    static constexpr uint32_t kWatchMask =
        IN_CLOSE_WRITE | IN_MODIFY | IN_ATTRIB |
        IN_CREATE | IN_DELETE |
        IN_MOVED_TO | IN_MOVED_FROM |
        IN_MOVE_SELF | IN_DELETE_SELF;

    int inotifyFd_{-1};
    std::map<int, std::string> watchDescToDir_;
    std::map<std::string, int> dirToWatchDesc_;
    std::set<std::string> watchedPaths_;
    mutable std::mutex mutex_;
};

} // namespace RSCGroup
