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
        const auto getInfo = [&](FILE_INFO_BY_HANDLE_CLASS infoClass, auto &info) {
            if (!GetFileInformationByHandleEx(h, infoClass, &info, sizeof(info))) {
                const auto error = GetLastError();
                qCCritical(lcLocalInfo) << u"GetFileInformationByHandleEx" << infoClass << u"failed on" << dirent.path().native()
                                        << OCC::Utility::formatWinError(error);
                _name.clear();
                return false;
            }
            return true;
        };
        FILE_BASIC_INFO basicInfo = {};
        FILE_STANDARD_INFO standardInfo = {};
        FILE_ID_INFO idInfo = {};
        if (!getInfo(FileBasicInfo, basicInfo) || !getInfo(FileStandardInfo, standardInfo) || !getInfo(FileIdInfo, idInfo)) {
            return;
        }
        _isHidden = basicInfo.FileAttributes & FILE_ATTRIBUTE_HIDDEN;
        // on NTFS the lower 64 bit of the 128 bit file id match the file index reported by GetFileInformationByHandle
        static_assert(sizeof(idInfo.FileId.Identifier) >= sizeof(_inode));
        std::memcpy(&_inode, idInfo.FileId.Identifier, sizeof(_inode));
        _size = standardInfo.EndOfFile.QuadPart;
        _modtime = FileSystem::fileTimeToTime_t(std::filesystem::file_time_type{std::filesystem::file_time_type::duration {
            basicInfo.LastWriteTime.QuadPart
        }});
        bool isSymLink = false;
        if (basicInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            // placeholders (cfapi, OneDrive, ...) are reparse points too, only name surrogates (symlinks, junctions) point to a different location
            FILE_ATTRIBUTE_TAG_INFO tagInfo = {};
            if (!getInfo(FileAttributeTagInfo, tagInfo)) {
                return;
            }
            isSymLink = IsReparseTagNameSurrogate(tagInfo.ReparseTag);
        }
        if (isSymLink) {
            _type = ItemTypeSymLink;
        } else if (basicInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
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
