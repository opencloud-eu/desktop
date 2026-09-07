# Smoke Testing

## 1. Installation

### Linux

**Pre-requisites:**

- Install `libfuse` system package.
- Download and make the client AppImage executable.

**Test Cases:**

1. Run the client AppImage.
   - [ ] Verify the client is running.

### Windows

**Test Cases:**

1. Install the client.
   - [ ] Verify the client is running.

## 2. Sync Account

**Test Cases:**

1. Add an account to the client.
   - [ ] Verify the account is added successfully.
2. Sync some files from the client to the server.
   - [ ] Verify the files sync successfully.
   - [ ] Verify the files are visible on the server.

**[Windows Only]**

3. Open dehydrated (online only - cloud icon) file from the file manager.
   - [ ] Verify the file content is downloaded.

## 3. Quit

**Test Cases:**

1. Close the client using the window close button.
   - [ ] Verify the client is closed.
   - [ ] Verify the client is running and available in the system tray.
2. Quit the client using the Quit button.
   - [ ] Verify the client is closed and not running in the system tray.

## 4. Crash Reporter

**Pre-requisites:**

- Run the client with the `--debug` option.
  ```bash
  <path/to/client/binary> --debug
  ```

> In Windows, binary usually located at: `C:\Program Files\WindowsApps\OpenCloudGmbH.OpenCloud\_%version_build_info%\bin`
>
> Refer to [this](https://www.guidingtech.com/how-to-access-windowsapps-folder-on-windows/) guide on how to access the WindowsApps folder.

**Test Cases:**

1. Crash the client using system tray debug actions: `System tray icon -> Debug actions -> Crash now - qFatal`.
   - [ ] Verify the crash log file is generated:
     - `/tmp/OpenCloud-crash.log`
     - `%USERPROFILE%\AppData\Local\Temp\OpenCloud-crash.log`
