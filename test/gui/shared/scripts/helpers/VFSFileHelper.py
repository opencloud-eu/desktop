import inspect
import ctypes
from ctypes import wintypes
from enum import IntFlag, Enum, unique

from helpers.ConfigHelper import is_windows


error_message = "'%s' function is only supported in Windows OS."

# ==========================
# Structures
# ==========================
class FILETIME(ctypes.Structure):
    _fields_ = [
        ("dwLowDateTime", wintypes.DWORD),
        ("dwHighDateTime", wintypes.DWORD),
    ]

class WIN32_FILE_ATTRIBUTE_DATA(ctypes.Structure):
    _fields_ = [
        ("dwFileAttributes", wintypes.DWORD),
        ("ftCreationTime", FILETIME),
        ("ftLastAccessTime", FILETIME),
        ("ftLastWriteTime", FILETIME),
        ("nFileSizeHigh", wintypes.DWORD),
        ("nFileSizeLow", wintypes.DWORD),
    ]

# Ref: https://learn.microsoft.com/en-us/windows/win32/fileio/file-attribute-constants
@unique
class FileAttributeConstants(IntFlag):
    __str__ = Enum.__str__
    FILE_ATTRIBUTE_PINNED = 0x00080000
    FILE_ATTRIBUTE_UNPINNED = 0x00100000
    FILE_ATTRIBUTE_ARCHIVE = 0x00000020

GetFileAttributesExW = None
GetCompressedFileSizeW = None

if is_windows():
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

    GetFileAttributesExW = kernel32.GetFileAttributesExW
    GetFileAttributesExW.argtypes = [
        wintypes.LPCWSTR,
        ctypes.c_int,
        ctypes.POINTER(WIN32_FILE_ATTRIBUTE_DATA),
    ]
    GetFileAttributesExW.restype = wintypes.BOOL

    GetCompressedFileSizeW = kernel32.GetCompressedFileSizeW
    GetCompressedFileSizeW.argtypes = [
        wintypes.LPCWSTR,
        ctypes.POINTER(wintypes.DWORD),
    ]
    GetCompressedFileSizeW.restype = wintypes.DWORD


def get_file_attributes(path):
    if is_windows():
        data = WIN32_FILE_ATTRIBUTE_DATA()
        success = GetFileAttributesExW(path, 0, ctypes.byref(data))
        if not success:
            raise ctypes.WinError(ctypes.get_last_error())
        attributes = FileAttributeConstants(data.dwFileAttributes)
        mask = (
            FileAttributeConstants.FILE_ATTRIBUTE_PINNED |
            FileAttributeConstants.FILE_ATTRIBUTE_UNPINNED |
            FileAttributeConstants.FILE_ATTRIBUTE_ARCHIVE
        )
        return attributes & mask
    raise OSError(error_message % inspect.currentframe().f_back.f_code.co_name)


def get_compressed_file_size(path):
    if is_windows():
        high = wintypes.DWORD(0)
        low = GetCompressedFileSizeW(path, ctypes.byref(high))

        if low == 0xFFFFFFFF:
            err = ctypes.get_last_error()
            if err != 0:
                raise ctypes.WinError(err)

        return (high.value << 32) | low
    raise OSError(error_message % inspect.currentframe().f_back.f_code.co_name)


def resource_archived(resource_path):
    if is_windows():
        return bool(get_file_attributes(resource_path) & FileAttributeConstants.FILE_ATTRIBUTE_ARCHIVE)
    raise OSError(error_message % inspect.currentframe().f_back.f_code.co_name)


def resource_pinned(resource_path):
    if is_windows():
        return bool(get_file_attributes(resource_path) & FileAttributeConstants.FILE_ATTRIBUTE_PINNED)
    raise OSError(error_message % inspect.currentframe().f_back.f_code.co_name)


def resource_unpinned(resource_path):
    if is_windows():
        return bool(get_file_attributes(resource_path) & FileAttributeConstants.FILE_ATTRIBUTE_UNPINNED)
    raise OSError(error_message % inspect.currentframe().f_back.f_code.co_name)


def is_placeholder_resource(resource_path):
    if is_windows():
        size_on_disk = get_compressed_file_size(resource_path)
        unpinned = resource_unpinned(resource_path)
        pinned = resource_pinned(resource_path)
        archived = resource_archived(resource_path)
        return (not size_on_disk or unpinned) and not (pinned and archived)
    raise OSError(error_message % inspect.currentframe().f_back.f_code.co_name)


def is_file_downloaded(resource_path):
    if is_windows():
        size_on_disk = get_compressed_file_size(resource_path)
        pinned = resource_pinned(resource_path)
        unpinned = resource_unpinned(resource_path)
        archived = resource_archived(resource_path)
        return size_on_disk and (pinned or archived) and not unpinned
    raise OSError(error_message % inspect.currentframe().f_back.f_code.co_name)
