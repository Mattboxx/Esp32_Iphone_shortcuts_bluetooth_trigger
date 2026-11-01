#!/usr/bin/env python3
"""
Build every firmware variant and produce ready-to-flash files in firmware/:

  firmware/bt-trigger-<chip>.factory.bin   merged image, write at 0x0
  firmware/manifest.json                    used by the web flasher

Usage:  python scripts/make_release.py            (build + merge)
        python scripts/make_release.py --no-build (merge existing .pio builds)
"""
import hashlib
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "firmware"

# env name -> (esptool chip, bootloader offset, transport, family label)
VARIANTS = {
    "esp32":   ("esp32",   0x1000, "classic", "ESP32"),
}

CONFIG_OFFSET = 0x180000  # "btcfg" partition, see partitions.csv


def fw_version():
    text = (ROOT / "src" / "config.h").read_text()
    return re.search(r'#define BTT_FW_VERSION\s+"([^"]+)"', text).group(1)


def run(cmd):
    print("+", " ".join(cmd), flush=True)
    subprocess.check_call(cmd, cwd=ROOT)


def main():
    build = "--no-build" not in sys.argv
    OUT.mkdir(exist_ok=True)
    version = fw_version()
    manifest = {"version": version, "config_offset": CONFIG_OFFSET, "builds": []}

    if build:
        run(["pio", "run"] + sum((["-e", env] for env in VARIANTS), []))
    for env, (chip, boot_off, transport, family) in VARIANTS.items():
        b = ROOT / ".pio" / "build" / env
        name = f"bt-trigger-{env}.factory.bin"
        run([sys.executable, "-m", "esptool", "--chip", chip, "merge_bin",
             "-o", str(OUT / name),
             "--flash_mode", "dio", "--flash_size", "2MB",
             hex(boot_off), str(b / "bootloader.bin"),
             "0x8000", str(b / "partitions.bin"),
             "0x10000", str(b / "firmware.bin")])
        data = (OUT / name).read_bytes()
        manifest["builds"].append({
            "chip": family,
            "env": env,
            "transport": transport,
            "file": name,
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })

    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"\nfirmware v{version} ready in {OUT}")


if __name__ == "__main__":
    main()
