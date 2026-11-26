#!/usr/bin/env python3
"""
Optional setup helper for Rogue Galaxy Remaster's local 4x AI BC7 texture cache.
Downloads the official MIT-licensed realesrgan-ncnn-vulkan Linux release
(which includes Vulkan binaries + realesr-animevideov3 / realesrgan-x4plus-anime
models) into $XDG_CACHE_HOME/RogueGalaxy/ai-textures-bc7/bin (or $RG_AI_CACHE_DIR/bin).

Once installed, Remaster mode automatically upscales 3D textures to 4x in the
background using the Steam Deck's Vulkan GPU and caches them on disk as hardware
BC7 (.dds) with mipmaps (0% GPU cost per frame in-game).
"""

import os
import shutil
import stat
import sys
import tempfile
import urllib.request
import zipfile

RELEASE_URL = (
    "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/"
    "realesrgan-ncnn-vulkan-20220424-ubuntu.zip"
)


def default_cache_dir() -> str:
    if os.environ.get("RG_AI_CACHE_DIR"):
        return os.environ["RG_AI_CACHE_DIR"]
    xdg = os.environ.get("XDG_CACHE_HOME")
    if xdg:
        return os.path.join(xdg, "RogueGalaxy", "ai-textures-bc7")
    return os.path.join(os.path.expanduser("~"), ".cache", "RogueGalaxy", "ai-textures-bc7")


def main() -> int:
    cache_dir = sys.argv[1] if len(sys.argv) > 1 else default_cache_dir()
    bin_dir = os.path.join(cache_dir, "bin")
    models_dir = os.path.join(bin_dir, "models")
    os.makedirs(models_dir, exist_ok=True)

    print(f"[ai-cache] Installing realesrgan-ncnn-vulkan into: {bin_dir}")
    print(f"[ai-cache] Downloading {RELEASE_URL} ...")

    with tempfile.TemporaryDirectory() as tmp:
        zip_path = os.path.join(tmp, "realesrgan.zip")
        urllib.request.urlretrieve(RELEASE_URL, zip_path)
        with zipfile.ZipFile(zip_path, "r") as zf:
            zf.extractall(tmp)

        exe_src = os.path.join(tmp, "realesrgan-ncnn-vulkan")
        if not os.path.isfile(exe_src):
            print("[ai-cache] Error: realesrgan-ncnn-vulkan binary not found in archive", file=sys.stderr)
            return 1
        exe_dst = os.path.join(bin_dir, "realesrgan-ncnn-vulkan")
        shutil.copy2(exe_src, exe_dst)
        os.chmod(exe_dst, os.stat(exe_dst).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        src_models = os.path.join(tmp, "models")
        if os.path.isdir(src_models):
            for name in os.listdir(src_models):
                shutil.copy2(os.path.join(src_models, name), os.path.join(models_dir, name))

    print("[ai-cache] Done! Available models:")
    for name in sorted(os.listdir(models_dir)):
        if name.endswith(".param"):
            print(f"  - {name[:-6]}")
    print("[ai-cache] Launch Rogue Galaxy with Remaster mode enabled; 4x BC7 textures will build automatically.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
