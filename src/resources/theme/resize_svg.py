import subprocess
from pathlib import Path

themeDir = Path(__file__).parent
for res in [16, 24, 32, 48, 64, 128, 256,  512, 1024]:
    subprocess.run(["inkscape", themeDir / "universal/opencloud-icon.svg",  "-w", str(res), "-h", str(res),  "--export-filename", themeDir / f"colored/{res}-opencloud-icon.png"])
    subprocess.run(["inkscape", themeDir / "universal/opencloud-icon-sidebar.svg",  "-w", str(res), "-h", str(res),  "--export-filename", themeDir / f"colored/{res}-opencloud-icon-sidebar.png"])

for res in [44, 150, 310]:
    subprocess.run(["inkscape", themeDir / "universal/opencloud-icon.svg",  "-w", str(res), "-h", str(res),  "--export-filename", themeDir / f"colored/{res}-opencloud-icon-ms.png"])
