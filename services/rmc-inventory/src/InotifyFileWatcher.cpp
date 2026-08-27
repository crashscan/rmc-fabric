#include "InotifyFileWatcher.h"

#include <glog/logging.h>

#include <cerrno>
#include <filesystem>
#include <set>
#include <unistd.h>

namespace RSCGroup {

InotifyFileWatcher::InotifyFileWatcher()
{
    inotifyFd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotifyFd_ < 0) {
        LOG(ERROR) << "InotifyFileWatcher: inotify_init1 failed, errno=" << errno;
    }
}

InotifyFileWatcher::~InotifyFileWatcher()
{
    std::scoped_lock lock(mutex_);
    if (inotifyFd_ >= 0) {
        for (const auto& [wd, _] : watchDescToDir_) {
            ::inotify_rm_watch(inotifyFd_, wd);
        }
        ::close(inotifyFd_);
    }
}

std::string InotifyFileWatcher::normalizePath(const std::string& path)
{
    if (path.empty()) {
        return {};
    }
    return std::filesystem::path(path).lexically_normal().string();
}

void InotifyFileWatcher::rearmDirectoryWatch(const std::string& dir)
{
    if (inotifyFd_ < 0 || dir.empty() || dirToWatchDesc_.contains(dir)) {
        return;
    }

    const int wd = ::inotify_add_watch(inotifyFd_, dir.c_str(), kWatchMask);
    if (wd < 0) {
        LOG(WARNING) << "InotifyFileWatcher: re-arm failed for '" << dir
                     << "', errno=" << errno << " — reconcile covers it";
        return;
    }

    dirToWatchDesc_[dir] = wd;
    watchDescToDir_[wd] = dir;
}

void InotifyFileWatcher::retryMissingDirectoryWatches()
{
    for (const auto& watchedPath : watchedPaths_) {
        auto dir = std::filesystem::path(watchedPath).parent_path().string();
        if (dir.empty()) {
            dir = ".";
        } else {
            dir = normalizePath(dir);
        }
        if (!dir.empty() && !dirToWatchDesc_.contains(dir)) {
            rearmDirectoryWatch(dir);
        }
    }
}

void InotifyFileWatcher::markAllWatchedPathsUnderDirChanged(const std::string& dir,
                                                            std::set<std::string>& changed) const
{
    std::filesystem::path dirPath = std::filesystem::path(dir).lexically_normal();
    if (dirPath.empty()) {
        dirPath = ".";
    }

    for (const auto& p : watchedPaths_) {
        auto parent = std::filesystem::path(p).parent_path().lexically_normal();
        if (parent.empty()) {
            parent = ".";
        }
        if (parent == dirPath) {
            changed.insert(p);
        }
    }
}

void InotifyFileWatcher::watchPath(const std::string& path)
{
    std::scoped_lock lock(mutex_);
    const auto normalizedPath = normalizePath(path);
    if (inotifyFd_ < 0 || normalizedPath.empty()) {
        return;
    }

    watchedPaths_.insert(normalizedPath);

    auto dir = std::filesystem::path(normalizedPath).parent_path().string();
    if (dir.empty()) {
        dir = ".";
    } else {
        dir = normalizePath(dir);
    }

    if (dir.empty()) {
        return;
    }

    rearmDirectoryWatch(dir);
}

void InotifyFileWatcher::maintain()
{
    std::scoped_lock lock(mutex_);
    retryMissingDirectoryWatches();
}

std::vector<std::string> InotifyFileWatcher::consumeChangedPaths()
{
    std::scoped_lock lock(mutex_);

    std::vector<std::string> changed;
    if (inotifyFd_ < 0) {
        return changed;
    }

    std::set<std::string> dedup;
    alignas(struct inotify_event) char buf[4096];

    for (;;) {
        const ssize_t len = ::read(inotifyFd_, buf, sizeof(buf));
        if (len < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN) {
                break;
            }
            LOG(ERROR) << "InotifyFileWatcher: read failed, errno=" << errno;
            break;
        }
        if (len == 0) {
            break;
        }

        for (char* p = buf; p < buf + len; ) {
            const auto* ev = reinterpret_cast<const struct inotify_event*>(p);

            if ((ev->mask & IN_Q_OVERFLOW) != 0) {
                LOG(WARNING) << "InotifyFileWatcher: event queue overflow; treating all watched paths as changed";
                dedup.insert(watchedPaths_.begin(), watchedPaths_.end());
                p += sizeof(struct inotify_event) + ev->len;
                continue;
            }

            const auto wdIt = watchDescToDir_.find(ev->wd);
            if (wdIt != watchDescToDir_.end() &&
                (ev->mask & (IN_IGNORED | IN_MOVE_SELF | IN_DELETE_SELF)) != 0) {
                const std::string dir = wdIt->second;
                watchDescToDir_.erase(wdIt);
                dirToWatchDesc_.erase(dir);

                rearmDirectoryWatch(dir);
                markAllWatchedPathsUnderDirChanged(dir, dedup);

                p += sizeof(struct inotify_event) + ev->len;
                continue;
            }

            const auto dirIt = watchDescToDir_.find(ev->wd);
            if (dirIt != watchDescToDir_.end() && ev->len > 0 && ev->name[0] != '\0') {
                const auto fullPath = normalizePath(
                    (std::filesystem::path(dirIt->second) / ev->name).string());
                if (watchedPaths_.contains(fullPath)) {
                    dedup.insert(fullPath);
                }
            }

            p += sizeof(struct inotify_event) + ev->len;
        }
    }

    changed.assign(dedup.begin(), dedup.end());
    return changed;
}

} // namespace RSCGroup
