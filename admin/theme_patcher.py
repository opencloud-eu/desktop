import base64
import enum
import io
import json
import mimetypes
import re
import shutil
import sys
from pathlib import Path

from PIL import Image


class Patcher(object):
    class OS(enum.Flag):
        WINDOWS = 1
        LINUX = 2
        MACOS = 4

    def __init__(self, installDir : Path, themeFile : Path):
        self.os = Patcher.OS.WINDOWS if sys.platform == "win32" else Patcher.OS.LINUX if sys.platform == "linux" else Patcher.OS.MACOS
        self.installDir = Path(installDir)
        self.themeFile = Path(themeFile)

        with self.themeFile.open("rt") as f:
            self.theme = json.load(f)["clients"]["desktop"]

        self.applicationIcon = base64.b64decode(self.theme["applicationIcon"]["base64"])

    # Platform

    @property
    def isWindows(self):
        return self.os == Patcher.OS.WINDOWS

    @property
    def isLinux(self):
        return self.os == Patcher.OS.LINUX

    @property
    def isMacOS(self):
        return self.os == Patcher.OS.MACOS

    @property
    def exeSuffix(self):
        return ".exe" if self.isWindows else ""

    # Theme

    @property
    def applicationName(self):
        return self.theme["applicationName"]

    @property
    def applicationDisplayName(self):
        return self.theme["applicationDisplayName"]

    @property
    def organizationDomain(self):
        return self.theme["organizationDomain"]

    @property
    def executableName(self):
        if self.isMacOS:
            return self.applicationName
        return self.applicationName.lower().replace(" ", "_")

    @property
    def applicationIconSuffix(self):
        mime = Patcher.mimeType(self.applicationIcon)
        suffix = mimetypes.guess_extension(mime)
        if not suffix:
            raise ValueError(f"No file extension known for {mime}")
        return suffix

    @property
    def bundleRoot(self):
        return self.installDir / f"Applications/KDE/{self.applicationName}.app"

    # Helpers

    @staticmethod
    def mimeType(data : bytes):
        signatures = {
            b"\x89PNG\r\n\x1a\n": "image/png",
            b"\xff\xd8\xff": "image/jpeg",
        }
        for magic, mime in signatures.items():
            if data.startswith(magic):
                return mime
        # TODO: pillow alone does not support svg
        #if b"<svg" in data[:1024]:
        #    return "image/svg+xml"
        raise ValueError("Unsupported image format")

    @staticmethod
    def writeData(data : bytes, destFile : Path):
        with destFile.open("wb") as f:
            f.write(data)

    def renamedFileName(self, name : str):
        return name.replace("opencloud", self.executableName)

    # Patch steps

    def installTheme(self):
        if self.isMacOS:
            dest = self.bundleRoot / "Contents/Resources/opencloud_theme.json"
        else:
            dest = self.installDir / "bin/opencloud_theme.json"
        print(f"Copying {self.themeFile} to {dest}")
        shutil.copyfile(self.themeFile, dest)

    def renameBinaries(self):
        if self.isMacOS:
            oldBundle = self.bundleRoot.parent / "OpenCloud.app"
            print(f"Renaming {oldBundle} to {self.bundleRoot}")
            oldBundle.rename(self.bundleRoot)
            binayDir = self.bundleRoot / "Contents" / "MacOS"
        else:
            binayDir = self.installDir / "bin"
        for app in ["opencloud", "opencloudcmd"]:
            oldBin = binayDir / f"{app}{self.exeSuffix}"
            newBin = oldBin.with_name(self.renamedFileName(oldBin.name))
            print(f"Renaming {oldBin} to {newBin}")
            oldBin.rename(newBin)

    def patchInfoPlist(self):
        infoPlist = self.bundleRoot / "Contents/Info.plist"
        content = infoPlist.read_text()
        content = (content.replace("<string>OpenCloud Desktop</string>", f"<string>{self.applicationDisplayName}</string>")
                   .replace("<string>OpenCloud</string>", f"<string>{self.applicationName}</string>"))
        infoPlist.write_text(content)

        infoPlist = self.bundleRoot / "Contents/PlugIns/FinderSyncExt.appex/Contents/Info.plist"
        content = infoPlist.read_text()
        content = (content.replace("<string>OpenCloud Desktop Extensions</string>", f"<string>{self.applicationDisplayName} Extensions</string>")
                   .replace("eu.opencloud.desktop", self.organizationDomain))
        infoPlist.write_text(content)

    def patchMackPkg(self):
        for script in ["macosx.pkgproj", "pre_install.sh", "post_install.sh"]:
            script = self.installDir / "OpenCloudAssets" / script
            content = script.read_text()
            content = (content.replace("OpenCloud Desktop", self.applicationDisplayName)
                       .replace("eu.opencloud.desktop", self.organizationDomain)
                       .replace("OpenCloud", self.applicationName))
            script.write_text(content)

    def patchDesktopFiles(self):
        for desktopFile in ["opencloud.desktop", "opencloudcmd.desktop"]:
            desktopFile = self.installDir / "share/applications" / desktopFile
            newDesktopFile = desktopFile.with_name(self.renamedFileName(desktopFile.name))
            print(f"Renaming {desktopFile} to {newDesktopFile}")
            desktopFile.rename(newDesktopFile)
            content = newDesktopFile.read_text()
            content = (content.replace("opencloud", self.executableName)
                       .replace("OpenCloud Desktop", self.applicationDisplayName))
            newDesktopFile.write_text(content)


    @staticmethod
    def iconSize(icon : Path):
        # hicolor icons live in <size>x<size>/apps/, fall back to a size in the file name
        for part in reversed(icon.parts):
            match = re.search(r"(\d+)x(\d+)", part)
            if match:
                return int(match.group(1)), int(match.group(2))
        raise ValueError(f"Failed to infer icon size from {icon}")

    def resizedApplicationIcon(self, size : tuple[int, int], format = "PNG"):
        with Image.open(io.BytesIO(self.applicationIcon)) as image:
            out = io.BytesIO()
            image.convert("RGBA").resize(size, Image.Resampling.LANCZOS).save(out, format=format)
            return out.getvalue()

    def patchFreeDesktopIcons(self):
        iconDir = self.installDir / "share/icons/hicolor"
        isSvg = Patcher.mimeType(self.applicationIcon) == "image/svg+xml"
        for icon in iconDir.rglob("*.png"):
            if "opencloud" in icon.name:
                print(f"Removing {icon}")
                icon.unlink()
                newIcon = icon.with_name(self.renamedFileName(icon.name))
                if isSvg:
                    newIcon = newIcon.with_suffix(self.applicationIconSuffix)
                    print(f"Writing {newIcon}")
                    Patcher.writeData(self.applicationIcon, newIcon)
                else:
                    size = Patcher.iconSize(icon)
                    print(f"Writing {newIcon} ({size[0]}x{size[1]})")
                    Patcher.writeData(self.resizedApplicationIcon(size), newIcon)

    def patchAppxIcons(self):
        iconDir = self.installDir / "OpenCloudAssets"
        for size in [44, 150]:
            newIcon = iconDir / f"{size}-opencloud-icon-ms.png"
            print(f"Writing {newIcon} ({size}x{size})")
            Patcher.writeData(self.resizedApplicationIcon((size, size)), newIcon)

    def patchMacIcons(self):
        iconDir = self.bundleRoot / "Contents/Resources"
        for size in [512]:
            newIcon = iconDir / f"opencloud.icns"
            print(f"Writing {newIcon} ({size}x{size})")
            Patcher.writeData(self.resizedApplicationIcon((size, size), format="ICNS"), newIcon)

    def patchAppStreamInfo(self):
        appStreamInfo = self.installDir / "share/metainfo/eu.opencloud.desktop.opencloud.metainfo.xml"
        if appStreamInfo.exists():
            # TODO: do we want to provide those?
            print(f"Removing {appStreamInfo}")
            appStreamInfo.unlink()

    def runStep(self, name : str, step):
        print(f"Patching {name}")
        step()
        print(f"Patching {name} complete")

    def patch(self):
        self.runStep("binaries", self.renameBinaries)
        self.runStep("theme", self.installTheme)
        if self.isLinux:
            self.runStep("desktop files", self.patchDesktopFiles)
            self.runStep("icons", self.patchFreeDesktopIcons)
            self.runStep("appstream meta info", self.patchAppStreamInfo)
        if self.isWindows:
            self.runStep("icons", self.patchAppxIcons)
        if self.isMacOS:
            self.runStep("patch Info.plist", self.patchInfoPlist)
            self.runStep("icons", self.patchMacIcons)
            self.runStep("macOS package", self.patchMackPkg)
        print("Patching complete")


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <installDir> <themeFile>")
        sys.exit(1)

    Patcher(sys.argv[1], sys.argv[2]).patch()
