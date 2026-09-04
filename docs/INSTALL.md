# Installing TerraForge

Three ways in, in increasing order of effort:

| | Windows | macOS | Linux |
|---|---|---|---|
| **Ready-made installer** | `TerraForge-<version>-Setup.exe` | `TerraForge-<version>.dmg` | — |
| **One click from source** | double-click `install.bat` | double-click `install.command` | `./scripts/install.sh` |
| **By hand** | [below](#building-by-hand) | [below](#building-by-hand) | [below](#building-by-hand) |

Everything TerraForge needs is either fetched automatically or already on your
machine. Nothing needs administrator rights.

---

## What TerraForge requires

| | Minimum |
|---|---|
| **Windows** | Windows 10 (1809) or 11, 64-bit |
| **macOS** | macOS 11 Big Sur or newer, Intel or Apple silicon |
| **Linux** | any current distribution with X11 or Wayland |
| **Graphics** | OpenGL 4.3 on Windows and Linux, OpenGL 4.1 on macOS — any GPU since roughly 2012 |
| **Memory** | 8 GB, 16 GB for terrains above 2048² |
| **Disk** | 2 GB for the sources and build, plus whatever material sets you download |

Python 3.9+ is optional. Without it TerraForge runs and renders in its own
viewport; with it you also get the offline path tracers and the AI assistant.

---

## Windows

### The installer

Download `TerraForge-<version>-Setup.exe` and run it. It installs into
`%LOCALAPPDATA%\Programs\TerraForge` for your account only, so Windows does not
ask for administrator rights, and it offers to associate `.gpxt` project files.

A portable `TerraForge-<version>-win64.zip` is published alongside it: unzip it
anywhere and run `geekatplay_studio.exe`. Same files, no installer.

### One click from source

```
git clone https://github.com/GeekatplayStudio/TerraForge.art.git
cd TerraForge.art
```

Then **double-click `install.bat`**. It will:

1. check for git, CMake, Ninja, a C++20 compiler and Python, and offer to
   install anything missing with `winget`;
2. download the third-party sources into `external/`;
3. build;
4. copy the result to `%LOCALAPPDATA%\Programs\TerraForge` and make Start Menu
   and Desktop shortcuts.

Useful flags, if you run it from a terminal:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\install.ps1 -Dev
powershell -ExecutionPolicy Bypass -File scripts\install.ps1 -Prefix "D:\Apps\TerraForge"
powershell -ExecutionPolicy Bypass -File scripts\install.ps1 -NoBuildTools
```

`-Dev` builds but installs nothing, which is what you want when you are working
on the code. `-NoBuildTools` refuses to install anything and tells you what is
missing instead.

### Uninstalling

Installed with the setup program: **Settings → Apps → TerraForge → Uninstall**.
Installed with `install.bat`: run `uninstall.ps1` in the install folder, or just
delete the folder and the two shortcuts.

Either way your preferences, autosaves and downloaded materials in
`%LOCALAPPDATA%\GeekatplayTerraForge` are left alone. Delete that folder too if
you want no trace.

---

## macOS

### The disk image

Open `TerraForge-<version>.dmg` and drag **TerraForge** onto **Applications**.

The first launch needs one extra step, because these builds are not notarised
by Apple: **right-click TerraForge in Applications, choose Open, then Open
again**. macOS remembers the decision, so this is a one-time thing. If you
prefer the terminal:

```bash
xattr -dr com.apple.quarantine /Applications/TerraForge.app
```

### One click from source

```bash
git clone https://github.com/GeekatplayStudio/TerraForge.art.git
cd TerraForge.art
```

Then **double-click `install.command`** in Finder (or run `./scripts/install.sh`).
It will:

1. check for the Xcode Command Line Tools, CMake and Ninja, installing the last
   two with Homebrew if you have it;
2. download the third-party sources into `external/`;
3. build;
4. assemble `TerraForge.app` and copy it into `/Applications` (or
   `~/Applications` if the first is not writable).

If Finder refuses to run the file, give it permission once:

```bash
chmod +x install.command
```

The prerequisites, if you would rather install them yourself:

```bash
xcode-select --install                    # compiler and headers
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew install cmake ninja
```

### About OpenGL on macOS

Apple deprecated OpenGL but has not removed it, and caps it at **4.1**.
TerraForge asks for a 4.1 core profile on macOS and compiles its shaders at
`#version 410 core`; it uses no feature above 4.1 (no compute shaders, no
shader storage buffers, no explicit binding layouts). Everything the studio
does — adaptive tessellation, volumetric clouds, the shadow pass, the render
passes — works within 4.1.

Metal is not used. If Apple removes OpenGL, TerraForge needs a Metal or
MoltenVK backend; that is a known future cost, not a present limitation.

### Uninstalling

Drag `TerraForge.app` to the Trash. Your settings are in
`~/Library/Application Support/GeekatplayTerraForge` — delete that too for a
clean slate.

---

## Linux

```bash
git clone https://github.com/GeekatplayStudio/TerraForge.art.git
cd TerraForge.art
./scripts/install.sh
```

It installs to `~/.local` and writes a desktop entry, so TerraForge appears in
your application menu. Make sure `~/.local/bin` is on your `PATH` to launch it
as `terraforge` from a terminal.

The script installs the X11 development headers GLFW needs on Debian and Ubuntu.
On other distributions install the equivalents of `libx11-dev libxrandr-dev
libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev` first.

---

## Building by hand

The same three steps on every platform.

**Windows**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\get_deps.ps1   # once
.\build.ps1
.\start.ps1
```

**macOS and Linux**

```bash
./scripts/get_deps.sh                                            # once
./build.sh
./start.sh
```

`get_deps` fetches Dear ImGui, GLFW, imgui-node-editor, GLM, GLAD, miniz,
nlohmann/json and stb into `external/`. None of them are committed to this
repository, and the two `get_deps` scripts fetch exactly the same versions.

Run the tests with `.\test.ps1` or `./test.sh`. All seven suites must pass
before a commit.

### Optional extras

```bash
pip install mitsuba                # path-traced offline rendering
pip install -r requirements.txt    # the whole Python layer
ollama pull llama3.1               # AI terrain from a description
ollama pull llava                  # AI terrain from a photograph
```

---

## Building the installers yourself

**Windows** — needs [Inno Setup 6](https://jrsoftware.org/isdl.php)
(`winget install --id JRSoftware.InnoSetup`) for the `.exe`; the portable ZIP
needs nothing:

```powershell
powershell -ExecutionPolicy Bypass -File packaging\windows\make_installer.ps1
```

Writes `dist\TerraForge-<version>-Setup.exe` and
`dist\TerraForge-<version>-win64.zip`.

**macOS** — needs nothing beyond the Command Line Tools:

```bash
./packaging/macos/make_dmg.sh --build
```

Writes `dist/TerraForge-<version>.dmg`. Add
`--sign "Developer ID Application: Your Name (TEAMID)"` if you have a
certificate; without one the bundle is ad-hoc signed, which is enough to launch
but still shows Gatekeeper's warning on another Mac.

A universal binary, if you want one build for both Mac architectures:

```bash
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_BUILD_TYPE=Release
cmake --build build
./packaging/macos/make_dmg.sh
```

---

## Where TerraForge keeps your files

| | Windows | macOS | Linux |
|---|---|---|---|
| Preferences, autosaves, material cache | `%LOCALAPPDATA%\GeekatplayTerraForge` | `~/Library/Application Support/GeekatplayTerraForge` | `~/.local/share/GeekatplayTerraForge` |
| Logs and crash reports | `logs\` beside the executable | same | same |
| Scripting inbox (Python API, MCP) | `%LOCALAPPDATA%\GeekatplayTerraForge\api` | `~/Library/Application Support/GeekatplayTerraForge/api` | `~/.local/share/GeekatplayTerraForge/api` |

Running from a source checkout, a `terraforge_prefs.json` sitting in the working
directory wins over the per-user copy, so a developer's settings stay with the
checkout they belong to.

---

## When it does not work

**"OpenGL 4.3 required" / the window never appears.** Update your graphics
driver. On a laptop with two GPUs, force TerraForge onto the discrete one
(NVIDIA Control Panel, or Windows Settings → Display → Graphics). Check
`logs/terraforge_<stamp>.log` — the GL version and renderer are recorded at
startup.

**`install.bat` says a tool is missing after installing it.** `winget` puts new
tools on the `PATH` of *new* processes. Close the window and double-click
`install.bat` again.

**Windows: "running scripts is disabled on this system".** Use the
`.bat` file rather than the `.ps1` directly; it already passes
`-ExecutionPolicy Bypass` for that one run and changes nothing permanently.

**macOS: "TerraForge is damaged and can't be opened".** That is Gatekeeper on an
un-notarised download, not a damaged file. Right-click → Open, or
`xattr -dr com.apple.quarantine /Applications/TerraForge.app`.

**macOS: the build fails at `glad`.** `get_deps.sh` generates the OpenGL loader
with Python. Install Python 3 (`brew install python`) and run it again.

**Linux: CMake cannot find X11.** Install the development headers listed under
[Linux](#linux) above.

**The offline renderers do nothing.** They are Python. Check that `python3 -m
mitsuba` imports, and look at the render log the Render panel names. TerraForge
itself never depends on Python — the viewport renderer is built in.

Every session writes `logs/terraforge_<stamp>.log`, flushed line by line, and a
crash adds `logs/crash_<stamp>.txt` with a stack. Attach both to a bug report:
they are the difference between a fix and a guess.
