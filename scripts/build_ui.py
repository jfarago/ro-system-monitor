# PlatformIO pre-script: gzip ui/* into data/*.gz for LittleFS (`pio run -t uploadfs`).
import gzip
import os
import shutil

Import("env")  # noqa: F821  (provided by PlatformIO)

root = env["PROJECT_DIR"]  # noqa: F821
src_dir = os.path.join(root, "ui")
out_dir = os.path.join(root, "data")

if os.path.isdir(src_dir):
    shutil.rmtree(out_dir, ignore_errors=True)
    os.makedirs(out_dir)
    total = 0
    for name in sorted(os.listdir(src_dir)):
        path = os.path.join(src_dir, name)
        if not os.path.isfile(path):
            continue
        with open(path, "rb") as f:
            raw = f.read()
        gz = gzip.compress(raw, 9, mtime=0)
        with open(os.path.join(out_dir, name + ".gz"), "wb") as f:
            f.write(gz)
        total += len(gz)
        print("ui: %s %d -> %d bytes gz" % (name, len(raw), len(gz)))
    print("ui: total %d bytes gzipped" % total)
