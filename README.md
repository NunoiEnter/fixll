# fixll — run LimeLight Lemonade Jam (KiriKiri game) on Linux

A one-shot installer that drops in a compatible `version.dll` shim and (optionally)
the English patch, so the game launches under Wine / Heroic + GE-Proton on **any**
Linux distro.

---

## If you landed here from a search

You probably saw one of these errors when trying to run the game:

- **`Malformed exe/dll detected: limelight_lj.exe`**
- `wine: Call from ... to unimplemented function VERSION.dll.GetFileVersionInfoSizeW, aborting`
- game window shows **`Fatal error unknown`**

The first is the KiriKiri **`bootStrap`** loader check, which can stop the game
from starting under Wine. The second happens when the `version.dll` doesn't
implement the version APIs the game calls. `fixll` addresses both.

---

## What it does

- Ships a **prebuilt 32-bit `VERSION.dll`** (a Windows PE binary, so it works
  identically on NixOS, Debian/Ubuntu, Arch, Fedora, openSUSE, Alpine, … — no
  per-distro build step required).
- Provides a compatible `version.dll` that hooks `LoadLibraryW`, finds the
  `bootStrap` module, and adjusts the two check patterns so the game starts.
- Implements the version APIs for real (reads the PE version resource), so Wine
  no longer aborts on `GetFileVersionInfoSizeW`.
- Optionally downloads and installs the **English MTL patch (VNDB r142645, V1.20)**
  by dropping in the translated `patch2.xp3` (KiriKiri auto-mounts `*.xp3`, so it
  just works).
- Optionally sets `WINEDLLOVERRIDES=version=n` in the game's Heroic config.

## Credit

The `bootStrap` compatibility technique is **FuckBootStrap** by
[sst311212](https://github.com/sst311212/FuckBootStrap). All credit for the core
approach goes there. This repo only wraps it with a prebuilt binary + installer
and fixes the version-API build bug (see `LICENSE`).

## Usage

```bash
git clone <this-repo> fixll
cd fixll

# shim only (works for any KiriKiri bootStrap game):
./install.sh /path/to/limelight_lj

# shim + English patch:
./install.sh /path/to/limelight_lj --english

# shim + English patch + auto Heroic override:
./install.sh /path/to/limelight_lj --english --heroic
```

Then launch the game (via Heroic, or your Wine prefix).

### Heroic note (if you didn't use `--heroic`)
Set the game's environment override:

```
WINEDLLOVERRIDES=version=n
```

## Rebuilding from source (optional)

The shipped `VERSION.dll` is already built. To recompile yourself:

```bash
./build.sh      # detects your distro, installs mingw-w64 (i686), compiles
```

`build.sh` handles apt / dnf / pacman / zypper / apk. (NixOS users: use the
prebuilt DLL, or `nix build nixpkgs#pkgsCross.mingw32.stdenv.cc`.)

## Files

```
fixll/
├── README.md
├── LICENSE
├── VERSION.dll      # prebuilt 32-bit shim
├── install.sh       # the one-shot installer
├── build.sh         # optional cross-distro recompiler
└── src/             # source (credited), for rebuilding
    ├── dllmain.cpp
    ├── VERSION.def
    └── detours/
```

This is a compatibility tool for running your own legally obtained copy of the
game under Wine.
