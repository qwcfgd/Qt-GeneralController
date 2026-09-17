"""Build a relocatable public release without private algorithms from an already deployed CMake build."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--qt-major", choices=["5", "6"], required=True)
    args = parser.parse_args()
    build, output = args.build.resolve(), args.output.resolve()
    archive = output.parent/(output.name+".zip")
    if output.exists() or archive.exists():
        parser.error("Output directory or archive already exists; choose a new output name.")
    required = [build/"QtBootloader.exe", build/"tests/VerifyRelease.exe",
                build/f"tests/Qt{args.qt_major}Test.dll", build/"platforms/qwindows.dll",
                build/"dll/PLinApi.dll", build/"dll/PCANBasic.dll",
                ROOT/"docs/User-Guide.md"]
    for path in required:
        if not path.is_file():
            parser.error(f"Missing required file: {path}")
    output.mkdir(parents=True)
    shutil.copy2(required[0], output)
    shutil.copy2(required[1], output)
    shutil.copy2(required[2], output)
    for path in build.glob("*.dll"):
        if path.name.startswith(("Qt5", "Qt6", "libgcc_", "libstdc++", "libwinpthread")):
            shutil.copy2(path, output)
    for name in ("generic", "iconengines", "imageformats",
                 "networkinformation", "platforms", "styles", "tls"):
        if (build/name).is_dir():
            shutil.copytree(build/name, output/name)
    (output/"dll").mkdir()
    for name in ("PLinApi.dll", "PCANBasic.dll"):
        shutil.copy2(build/"dll"/name, output/"dll"/name)
    (output/"qt.conf").write_text("[Paths]\nPrefix=.\nPlugins=.\n", encoding="utf-8")
    (output/"profiles").mkdir()
    shutil.copytree(ROOT/"profiles/fixtures", output/"profiles/fixtures")
    profile = json.loads((ROOT/"profiles/stage6-can-lin-simulation.json").read_text(encoding="utf-8"))
    for channel in profile["channels"]:
        channel["flashPath"] = "fixtures/flash-driver.bin"
        channel["applicationPath"] = "fixtures/application.bin"
        channel["simulation"] = True
    (output/"profiles/simulation.json").write_text(
        json.dumps(profile, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    (output/"docs").mkdir()
    for path in (ROOT/"docs").glob("*.md"):
        shutil.copy2(path, output/"docs"/path.name)
    (output/"README.txt").write_text(
        "GENERAL BOOTLOADER CONTROLLER - InternalVer 0.2\n\n"
        "启动 QtBootloader.exe。首次只有 CAN01，不自动连接。\n"
        "完整说明：docs/User-Guide.md\n"
        "模拟验证：载入 profiles/simulation.json，连接所需通道后开始模拟下载。\n"
        "自动自检：运行 VerifyRelease.exe；不会连接或发送到物理总线。\n"
        "公开包不含安全访问算法；实机使用需另行配置已授权 DLL 和桥接程序。\n"
        "请保留整个目录；不要只复制 exe。保存配置需要目录可写。\n",
        encoding="utf-8-sig")
    inventory = {
        "applicationVersion": "0.2.1", "uiVersion": "InternalVer 0.2",
        "qtMajor": int(args.qt_major), "architecture": "Windows x64",
        "buildType": "Release", "physicalDownloadEnabled": False, "physicalDownloadBuses": [], "physicalECUValidated": False,
        "components": ["Qt runtime and plugins", "MinGW runtime",
                       "PEAK PCANBasic and PLIN API x64", "simulation fixtures"],
        "driverServicesIncluded": False,
    }
    (output/"release-info.json").write_text(
        json.dumps(inventory, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    manifest = {}
    for path in sorted(output.rglob("*")):
        if path.is_file():
            manifest[path.relative_to(output).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    (output/"manifest-sha256.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for path in sorted(output.rglob("*")):
            if path.is_file():
                z.write(path, path.relative_to(output.parent).as_posix())
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".zip.sha256").write_text(f"{digest}  {archive.name}\n", encoding="ascii")
    print(json.dumps({"directory": str(output), "archive": str(archive),
                      "files": len(manifest), "sha256": digest}, ensure_ascii=False))

if __name__ == "__main__":
    main()
