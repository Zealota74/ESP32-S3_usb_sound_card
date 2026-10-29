# patch_core.py - podmienia USBAudioCard.cpp w rdzeniu Arduino-ESP32 na wersję z repo
# Uruchamiany przed każdym buildem (extra_scripts = pre:tools/patch_core.py)

Import("env")
import os
import shutil
import filecmp

# --- źródło: <repo>/libraries/USBAudioCard.cpp --------------------------------
# PROJECT_DIR = katalog z platformio.ini, czyli <repo>/src/Platformio
REPO_ROOT = os.path.normpath(os.path.join(env["PROJECT_DIR"], "..", ".."))
SRC_FILE = os.path.join(REPO_ROOT, "libraries", "USBAudioCard.cpp")

if not os.path.isfile(SRC_FILE):
    env.Exit("patch_core: brak pliku " + SRC_FILE)

# --- cel: miejsce USBAudioCard.cpp w rdzeniu ------------------------------------
fw = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
if not fw or not os.path.isdir(fw):
    env.Exit("patch_core: nie znaleziono frameworka Arduino-ESP32")

dst_file = None
for root, dirs, files in os.walk(os.path.join(fw, "libraries")):
    if "USBAudioCard.cpp" in files:
        dst_file = os.path.join(root, "USBAudioCard.cpp")
        break

if dst_file is None:
    env.Exit("patch_core: w rdzeniu nie znaleziono USBAudioCard.cpp")

# --- kopiowanie (tylko gdy plik się różni) ---------------------------------------
if filecmp.cmp(SRC_FILE, dst_file, shallow=False):
    print("patch_core: USBAudioCard.cpp bez zmian ->", dst_file)
else:
    shutil.copy2(SRC_FILE, dst_file)
    print("patch_core: podmieniono USBAudioCard.cpp ->", dst_file)