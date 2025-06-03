

#include "nc_utility.h"

using namespace OCC;

bool Utility::registryKeyExists(HKEY hRootKey, const QString &subKey)
{
    HKEY hKey;

    REGSAM sam = KEY_READ | KEY_WOW64_64KEY;
    LONG result = RegOpenKeyEx(hRootKey, reinterpret_cast<LPCWSTR>(subKey.utf16()), 0, sam, &hKey);

    RegCloseKey(hKey);
    return result != ERROR_FILE_NOT_FOUND;
}

QVariant Utility::registryGetKeyValue(HKEY hRootKey, const QString &subKey, const QString &valueName)
{
    QVariant value;

    HKEY hKey;

    REGSAM sam = KEY_READ | KEY_WOW64_64KEY;
    LONG result = RegOpenKeyEx(hRootKey, reinterpret_cast<LPCWSTR>(subKey.utf16()), 0, sam, &hKey);
    Q_ASSERT(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    if (result != ERROR_SUCCESS)
        return value;

    DWORD type = 0, sizeInBytes = 0;
    result = RegQueryValueEx(hKey, reinterpret_cast<LPCWSTR>(valueName.utf16()), 0, &type, nullptr, &sizeInBytes);
    Q_ASSERT(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    if (result == ERROR_SUCCESS) {
        switch (type) {
        case REG_DWORD:
            DWORD dword;
            Q_ASSERT(sizeInBytes == sizeof(dword));
            if (RegQueryValueEx(hKey, reinterpret_cast<LPCWSTR>(valueName.utf16()), 0, &type, reinterpret_cast<LPBYTE>(&dword), &sizeInBytes)
                == ERROR_SUCCESS) {
                value = int(dword);
            }
            break;
        case REG_EXPAND_SZ:
        case REG_SZ: {
            QString string;
            string.resize(sizeInBytes / sizeof(QChar));
            result = RegQueryValueEx(hKey, reinterpret_cast<LPCWSTR>(valueName.utf16()), 0, &type, reinterpret_cast<LPBYTE>(string.data()), &sizeInBytes);

            if (result == ERROR_SUCCESS) {
                int newCharSize = sizeInBytes / sizeof(QChar);
                // From the doc:
                // If the data has the REG_SZ, REG_MULTI_SZ or REG_EXPAND_SZ type, the string may not have been stored with
                // the proper terminating null characters. Therefore, even if the function returns ERROR_SUCCESS,
                // the application should ensure that the string is properly terminated before using it; otherwise, it may overwrite a buffer.
                if (string.at(newCharSize - 1) == QLatin1Char('\0'))
                    string.resize(newCharSize - 1);
                value = string;
            }
            break;
        }
        case REG_BINARY: {
            QByteArray buffer;
            buffer.resize(sizeInBytes);
            result = RegQueryValueEx(hKey, reinterpret_cast<LPCWSTR>(valueName.utf16()), 0, &type, reinterpret_cast<LPBYTE>(buffer.data()), &sizeInBytes);
            if (result == ERROR_SUCCESS) {
                value = buffer.at(12);
            }
            break;
        }
        default:
            Q_UNREACHABLE();
        }
    }
    Q_ASSERT(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);

    RegCloseKey(hKey);
    return value;
}

bool Utility::registrySetKeyValue(HKEY hRootKey, const QString &subKey, const QString &valueName, DWORD type, const QVariant &value)
{
    HKEY hKey;
    // KEY_WOW64_64KEY is necessary because CLSIDs are "Redirected and reflected only for CLSIDs that do not specify InprocServer32 or InprocHandler32."
    // https://msdn.microsoft.com/en-us/library/windows/desktop/aa384253%28v=vs.85%29.aspx#redirected__shared__and_reflected_keys_under_wow64
    // This shouldn't be an issue in our case since we use shell32.dll as InprocServer32, so we could write those registry keys for both 32 and 64bit.
    // FIXME: Not doing so at the moment means that explorer will show the cloud provider, but 32bit processes' open dialogs (like the ownCloud client itself)
    // won't show it.
    REGSAM sam = KEY_WRITE | KEY_WOW64_64KEY;
    LONG result = RegCreateKeyEx(hRootKey, reinterpret_cast<LPCWSTR>(subKey.utf16()), 0, nullptr, 0, sam, nullptr, &hKey, nullptr);
    Q_ASSERT(result == ERROR_SUCCESS);
    if (result != ERROR_SUCCESS)
        return false;

    result = -1;
    switch (type) {
    case REG_DWORD: {
        DWORD dword = value.toInt();
        result = RegSetValueEx(hKey, reinterpret_cast<LPCWSTR>(valueName.utf16()), 0, type, reinterpret_cast<const BYTE *>(&dword), sizeof(dword));
        break;
    }
    case REG_EXPAND_SZ:
    case REG_SZ: {
        QString string = value.toString();
        result = RegSetValueEx(hKey, reinterpret_cast<LPCWSTR>(valueName.utf16()), 0, type, reinterpret_cast<const BYTE *>(string.constData()),
            (string.size() + 1) * sizeof(QChar));
        break;
    }
    default:
        Q_UNREACHABLE();
    }
    Q_ASSERT(result == ERROR_SUCCESS);

    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

bool Utility::registryDeleteKeyTree(HKEY hRootKey, const QString &subKey)
{
    HKEY hKey;
    REGSAM sam = DELETE | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY;
    LONG result = RegOpenKeyEx(hRootKey, reinterpret_cast<LPCWSTR>(subKey.utf16()), 0, sam, &hKey);
    Q_ASSERT(result == ERROR_SUCCESS);
    if (result != ERROR_SUCCESS)
        return false;

    result = RegDeleteTree(hKey, nullptr);
    RegCloseKey(hKey);
    Q_ASSERT(result == ERROR_SUCCESS);

    result |= RegDeleteKeyEx(hRootKey, reinterpret_cast<LPCWSTR>(subKey.utf16()), sam, 0);
    Q_ASSERT(result == ERROR_SUCCESS);

    return result == ERROR_SUCCESS;
}

bool Utility::registryDeleteKeyValue(HKEY hRootKey, const QString &subKey, const QString &valueName)
{
    HKEY hKey;
    REGSAM sam = KEY_WRITE | KEY_WOW64_64KEY;
    LONG result = RegOpenKeyEx(hRootKey, reinterpret_cast<LPCWSTR>(subKey.utf16()), 0, sam, &hKey);
    Q_ASSERT(result == ERROR_SUCCESS);
    if (result != ERROR_SUCCESS)
        return false;

    result = RegDeleteValue(hKey, reinterpret_cast<LPCWSTR>(valueName.utf16()));
    Q_ASSERT(result == ERROR_SUCCESS);

    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

bool Utility::registryWalkSubKeys(HKEY hRootKey, const QString &subKey, const std::function<void(HKEY, const QString &)> &callback)
{
    HKEY hKey;
    REGSAM sam = KEY_READ | KEY_WOW64_64KEY;
    LONG result = RegOpenKeyEx(hRootKey, reinterpret_cast<LPCWSTR>(subKey.utf16()), 0, sam, &hKey);
    Q_ASSERT(result == ERROR_SUCCESS);
    if (result != ERROR_SUCCESS)
        return false;

    DWORD maxSubKeyNameSize;
    // Get the largest keyname size once instead of relying each call on ERROR_MORE_DATA.
    result = RegQueryInfoKey(hKey, nullptr, nullptr, nullptr, nullptr, &maxSubKeyNameSize, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    Q_ASSERT(result == ERROR_SUCCESS);
    if (result != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return false;
    }

    QString subKeyName;
    subKeyName.reserve(maxSubKeyNameSize + 1);

    DWORD retCode = ERROR_SUCCESS;
    for (DWORD i = 0; retCode == ERROR_SUCCESS; ++i) {
        Q_ASSERT(unsigned(subKeyName.capacity()) > maxSubKeyNameSize);
        // Make the previously reserved capacity official again.
        subKeyName.resize(subKeyName.capacity());
        DWORD subKeyNameSize = subKeyName.size();
        retCode = RegEnumKeyEx(hKey, i, reinterpret_cast<LPWSTR>(subKeyName.data()), &subKeyNameSize, nullptr, nullptr, nullptr, nullptr);

        Q_ASSERT(result == ERROR_SUCCESS || retCode == ERROR_NO_MORE_ITEMS);
        if (retCode == ERROR_SUCCESS) {
            // subKeyNameSize excludes the trailing \0
            subKeyName.resize(subKeyNameSize);
            // Pass only the sub keyname, not the full path.
            callback(hKey, subKeyName);
        }
    }

    RegCloseKey(hKey);
    return retCode != ERROR_NO_MORE_ITEMS;
}

bool Utility::registryWalkValues(HKEY hRootKey, const QString &subKey, const std::function<void(const QString &, bool *)> &callback)
{
    HKEY hKey;
    REGSAM sam = KEY_QUERY_VALUE;
    LONG result = RegOpenKeyEx(hRootKey, reinterpret_cast<LPCWSTR>(subKey.utf16()), 0, sam, &hKey);
    Q_ASSERT(result == ERROR_SUCCESS);
    if (result != ERROR_SUCCESS) {
        return false;
    }

    DWORD maxValueNameSize = 0;
    result = RegQueryInfoKey(hKey, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &maxValueNameSize, nullptr, nullptr, nullptr);
    Q_ASSERT(result == ERROR_SUCCESS);
    if (result != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return false;
    }

    QString valueName;
    valueName.reserve(maxValueNameSize + 1);

    DWORD retCode = ERROR_SUCCESS;
    bool done = false;
    for (DWORD i = 0; retCode == ERROR_SUCCESS; ++i) {
        Q_ASSERT(unsigned(valueName.capacity()) > maxValueNameSize);
        valueName.resize(valueName.capacity());
        DWORD valueNameSize = valueName.size();
        retCode = RegEnumValue(hKey, i, reinterpret_cast<LPWSTR>(valueName.data()), &valueNameSize, nullptr, nullptr, nullptr, nullptr);

        Q_ASSERT(result == ERROR_SUCCESS || retCode == ERROR_NO_MORE_ITEMS);
        if (retCode == ERROR_SUCCESS) {
            valueName.resize(valueNameSize);
            callback(valueName, &done);

            if (done) {
                break;
            }
        }
    }

    RegCloseKey(hKey);
    return retCode != ERROR_NO_MORE_ITEMS;
}
