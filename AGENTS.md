# AGENTS.md

qimgv: Qt6 + C++23 image viewer for Windows (personal fork of easymodo/qimgv; `origin` = github.com/yeezylife/qimgv). Main branch; commit messages and many code comments are in Chinese — match that style.

## Build (Windows)

The canonical build environment is MSYS2 `CLANG64` (Clang), defined in `.github/workflows/*.yml`. Verify changes with the CI recipe:

```
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=23 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF \
  -DVIDEO_SUPPORT=OFF -DOPENCV_SUPPORT=ON -DCMAKE_PREFIX_PATH=/clang64
cmake --build build --config Release
```

- **Qt6 only** (6.11+, no Qt5 compatibility code — the repo no longer carries `QT_VERSION`/`USE_QT5` branches). CMake options (root `CMakeLists.txt`): `VIDEO_SUPPORT` (default ON, builds `plugins/player_mpv`), `OPENCV_SUPPORT` (default ON, needs OpenCV), `KDE_SUPPORT` (OFF).
- The strict CI (`qimgv-CI-Strict-Check.yml`) builds Debug with `-Wall -Wextra -Werror` plus ~40 extra warnings and clang-tidy; keep new code clean under those flags. Release CI uses `-march=x86-64-v3` + LTO (`qimgv-qt6-build.yml`).
- README mentions clang-format, but there is **no `.clang-format`/`.clang-tidy` file** in the repo — formatting is not enforced by tooling.

## Tests

`qimgv/tests/` is **not wired into CMake** (`qimgv/CMakeLists.txt` never calls `add_subdirectory(tests)`). The only test (`unit_tests`, `test_mapoverlay.cpp`, Qt Test) is not built and `ctest` runs nothing by default. Do not assume tests exist for a change.

## Repo rules & conventions

Rules from `.clinerules` (authoritative, keep them in mind on every change):
- Target is Qt 6.10 / C++23 on Windows; optimize performance while preserving compatibility.
- **Do not add caching unless you're sure the perf gain is large.**
- **Do not add logging or error handling casually.**

Other conventions:
- Hot paths / already-optimized spots are marked with `⭐` in Chinese comments (e.g. `directorymanager.cpp`, `mainwindow.cpp`). Prior perf work eliminates redundant stat/I/O (e.g. `QDir::entryInfoList()` cached metadata, `std::filesystem::directory_iterator`, unified time computation) rather than adding caches — continue that approach.
- Source files are UTF-8 without BOM; keep new comments/commits in Chinese.
- The thumbnail system was removed (breaking change vs upstream); don't reintroduce it.
- Personal project: no feature requests / upstream PR flow (per README); keep changes compatible with the existing fork.

## Layout

- `qimgv/main.cpp` — entrypoint; app info/env quirks are set here (e.g. `QT_PLUGIN_PATH=""` on Windows). `core.cpp/h` orchestrates the app; `settings.cpp`, `themestore.cpp`, `shortcutbuilder.cpp`, `proxystyle.cpp` are top-level support.
- `qimgv/components/` — `actionmanager`, `cache`, `directorymanager` (with `watchers/windows` and `watchers/linux`), `loader`, `scaler`, `scriptmanager`.
- `qimgv/gui/` — `viewers` (e.g. `imageviewerv2`), `panels` (`mainpanel`, `sidepanel`, `infobar`, `croppanel`), `overlays`, `dialogs`, `customwidgets`.
- `qimgv/sourcecontainers/`, `qimgv/utils/`, `qimgv/3rdparty/QtOpenCV` (built only when `OPENCV_SUPPORT=ON`).
- `plugins/player_mpv/` — video playback plugin, built only when `VIDEO_SUPPORT=ON`; on Windows it loads as `plugins/player_mpv.dll`.

## Translations

Single translation `qimgv/res/translations/zh_CN.ts`, listed in `TS_FILES` in `qimgv/CMakeLists.txt`. `qt_add_lupdate`/`qt_add_lrelease` handle `.ts`→`.qm` (the `qimgv_lupdate` target updates the `.ts`). New locale = add a `.ts` to `TS_FILES`.
