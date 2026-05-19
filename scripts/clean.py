import os

from pathlib import Path

for path in Path("..").rglob("desktop.ini"):
    os.remove(path)
