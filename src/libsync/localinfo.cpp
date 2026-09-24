// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 Hannah von Reth <h.vonreth@opencloud.eu>

#include "libsync/localinfo.h"

#include "libsync/filesystem.h"

#ifdef Q_OS_WIN
#include "libsync/common/utility_win.h"
#else
#include <sys/stat.h>
#endif

Q_LOGGING_CATEGORY(lcLocalInfo, "sync.discovery.localinfo", QtInfoMsg)

using namespace OCC;

class OCC::LocalInfoData : public QSharedData
{
public:
    LocalInfoData() = default;
    ~LocalInfoData() = default;

    LocalInfoData(const std::filesystem::directory_entry &dirent)
        : _path(dirent.path())
        , _name(FileSystem::fromFilesystemPath(_path.filename()))
    {
#ifdef Q_OS_WIN
        auto h = Utility::Handle::createHandle(dirent.path(), {.followSymlinks = false});
        if (!h) {
            qCWarning(lcLocalInfo) << dirent.path().native() << h.errorMessage();
            _name.clear();
            return;
        }
        BY_HANDLE_FILE_INFORMATION fileInfo = {};
        if (!GetFileInformationByHandle(h, &fileInfo)) {
            const auto error = GetLastError();
            qCCritical(lcLocalInfo) << u"GetFileInformationByHandle failed on" << dirent.path().native() << OCC::Utility::formatWinError(error);
            _name.clear();
            return;
        }
        _isHidden = fileInfo.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN;
        _inode = ULARGE_INTEGER{{ fileInfo.nFileIndexLow, fileInfo.nFileIndexHigh }}.QuadPart;
        _size = ULARGE_INTEGER{{ fileInfo.nFileSizeLow, fileInfo.nFileSizeHigh }}.QuadPart;
        _modtime = FileSystem::fileTimeToTime_t(std::filesystem::file_time_type{std::filesystem::file_time_type::duration {
            ULARGE_INTEGER{fileInfo.ftLastWriteTime.dwLowDateTime, fileInfo.ftLastWriteTime.dwHighDateTime}.QuadPart
        }});
        if (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            _type = ItemTypeSymLink;
        } else if (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            _type = ItemTypeDirectory;
        } else {
            _type = ItemTypeFile;
        }
#else
        struct stat sb;
        if (lstat(dirent.path().native().data(), &sb) < 0) {
            qCCritical(lcLocalInfo) << u"lstat failed on" << dirent.path().native();
            _name.clear();
            return;
        }
        _inode = sb.st_ino;
        _size = sb.st_size;
        _modtime = sb.st_mtime;
        if (S_ISLNK(sb.st_mode)) {
            _type = ItemTypeSymLink;
        } else if (S_ISDIR(sb.st_mode)) {
            _type = ItemTypeDirectory;
        } else if (S_ISREG(sb.st_mode)) {
            _type = ItemTypeFile;
        } else {
            _type = ItemTypeUnsupported;
        }
#ifdef Q_OS_MAC
        _isHidden = sb.st_flags & UF_HIDDEN;
#endif
#endif
    }

    std::filesystem::path _path;
    QString _name;
    ItemType _type = ItemTypeUnsupported;
    time_t _modtime = 0;
    int64_t _size = 0;
    uint64_t _inode = 0;
    bool _isHidden = false;
};

LocalInfo::LocalInfo()
    : d([] {
        static QExplicitlySharedDataPointer<LocalInfoData> nullData{new LocalInfoData{}};
        return nullData;
    }())
{
}


LocalInfo::LocalInfo(LocalInfo &&other) noexcept = default;

LocalInfo::~LocalInfo() = default;

LocalInfo::LocalInfo(const LocalInfo &other) = default;

LocalInfo &LocalInfo::operator=(const LocalInfo &other) = default;

LocalInfo::LocalInfo(const std::filesystem::directory_entry &dirent)
    : d(new LocalInfoData(dirent))
{
}

LocalInfo::LocalInfo(const std::filesystem::path &path)
    : LocalInfo(std::filesystem::directory_entry{path})
{
}

bool LocalInfo::isHidden() const
{
    return d->_isHidden;
}

QString LocalInfo::name() const
{
    return d->_name;
}

std::filesystem::path LocalInfo::path() const
{
    return d->_path;
}

time_t LocalInfo::modtime() const
{
    return d->_modtime;
}

int64_t LocalInfo::size() const
{
    return d->_size;
}

uint64_t LocalInfo::inode() const
{
    return d->_inode;
}

ItemType LocalInfo::type() const
{
    return d->_type;
}

void LocalInfo::setType(ItemType type)
{
    d->_type = type;
}

bool LocalInfo::isDirectory() const
{
    return d->_type == ItemTypeDirectory;
}

bool LocalInfo::isVirtualFile() const
{
    return d->_type == ItemTypeVirtualFile || d->_type == ItemTypeVirtualFileDownload;
}

bool LocalInfo::isSymLink() const
{
    return d->_type == ItemTypeSymLink;
}

bool LocalInfo::isValid() const
{
    return !d->_name.isNull();
}
