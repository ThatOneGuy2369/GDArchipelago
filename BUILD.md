# Building and installing the Geometry Dash Archipelago mod

The mod needs **Geode v6** (released Nov 24, 2026), which supports Geometry Dash **2.2091**, the
current Steam version. `mod.json` targets Geode 6.0.0 and GD 2.2091. If a future GD update breaks it,
change the `"win"` version in `mod.json` once Geode supports the new one, and build again.

## 1. Build the .geode file (pick one)

**A. GitHub, no tools on your PC (recommended)**
1. Make a free GitHub account and a new repository.
2. Upload everything in this `geode_mod` folder (including the hidden `.github` folder).
3. Open the repository's **Actions** tab. "Build Geode Mod" runs on its own (or press **Run workflow**).
4. When it's green, open the run, download **Build Output**, and unzip it: inside is `gdap.archipelago.geode`.

**B. On your PC**
1. Install Visual Studio 2022 (with "Desktop development with C++"), CMake, and the Geode CLI.
2. Run `geode sdk install` then `geode sdk install-binaries`.
3. In this folder: `geode build`. The `.geode` file appears in `build`.

## 2. Install it
1. Close Geometry Dash.
2. Put the `.geode` file into the `installer` folder, next to `install.bat`.
3. Double-click **install.bat**. It finds Geometry Dash, checks Geode is there, and copies the mod in.
   (By hand: copy the `.geode` into `Geometry Dash\geode\mods`.)

## 3. Play
1. Start Geometry Dash. Geode > Mods > **Geometry Dash Archipelago** > settings: set **Server** (like
   `archipelago.gg:53092`) and **Slot name**.
2. Press the **AP** button on the main menu > **Connect**.
3. Play. Checks, locks, items and traps are handled in game; gauntlets are reported from the AP panel.

## Testing the Archipelago logic
`tests/run_tests.sh` builds and runs the core tests with any C++17 compiler (no Geode). They include
15,000+ unlock decisions compared against the Python companion on real generated seeds.
