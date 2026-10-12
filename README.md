# armada-mods

Game mods for [Armada](https://github.com/armada-os/armada): folders of files
that Armada applies to a Steam game at launch without changing its install.
The ones here put a companion view on the second screen of dual-screen
devices.

| Mod | Game | Second screen |
|---|---|---|
| `second-screen-hollow-knight` | Hollow Knight | The live map, charms (change them by touch at a bench) and inventory |
| `second-screen-balatro` | Balatro | The blinds ahead, jokers with their current values, remaining deck, hand levels |
| `second-screen-crosscode` | CrossCode | Status, element switching and consumables by touch, quests |
| `second-screen-persona-3-reload` | Persona 3 Reload | Calendar, social stats and links, battle party, personas |
| `second-screen-chained-echoes` | Chained Echoes | Area map with your position, party, quest journal, items |

A mod reaches into its game's code, so a game update can break it. A mod
that stops understanding its game lets go of the second screen and the
game runs as usual.

[FORMAT.md](FORMAT.md) describes what a mod is and how to write one:
the folder layout, `mod.json`, hooks, the second-screen program and how
mod files reach the game.

## Layout

    <mod id>/            one folder per mod, named by its id
      mod.json           the manifest
      overlay/ hooks/ view/ ...   what ships as it is
      src/               source for compiled parts, not shipped
      build.sh           how the rest of the package is produced, not shipped
    screen-host/         the second-screen program the mods here use
    lib/                 code and build steps shared by the mods
    build.sh             builds packages
    refs.sh              collects game reference assemblies for Unity mods
    FORMAT.md            the mod format, for writing a mod

## Installing a mod

In Armada Store, open Game Mods. The mods here are listed under Featured;
"Add local .tar.gz" installs any other package.

By hand: unpack a release archive into
`~/.local/share/armada-mods/mods/` on the device. It contains one folder
named by the mod id. Delete that folder, and
`~/.local/share/armada-mods/data/<mod id>/`, to remove it.

## Building

    ./build.sh                     every mod
    ./build.sh second-screen-balatro

Packages land in `dist/<id>-<version>.tar.gz`.

A package is the mod's folder without `src/` and `build.sh`, plus whatever
its `build.sh` adds: the screen host, compiled code, and pinned third-party
software that is downloaded and checked against a checksum.
[THIRD-PARTY.md](THIRD-PARTY.md) lists what packages can contain.

Every build needs `bash`, `python3`, `curl` and `unzip`. Beyond that:

| Mod | Needs |
|---|---|
| Hollow Knight | `dotnet` 8 or `podman`, and the game's assemblies (below) |
| Persona 3 Reload | `x86_64-w64-mingw32-gcc` or `podman`, for a small Windows helper (`lib/ue4ss-late`) |
| Chained Echoes | `gcc` on an x86-64 machine, a cross compiler, or `podman`, for an x86-64 Linux library |
| Balatro, CrossCode | Nothing more |

A Unity mod compiles against the game's own assemblies. Those are not in
this repository. Collect them once from a copy of the game you own:

    ./refs.sh second-screen-hollow-knight "/path/to/Hollow Knight"

They go to `refs/<mod id>/`, which is ignored by git.

## Releasing

Each mod is released on its own, under a tag named `<mod id>/v<version>`
with its one package attached. Armada Store offers the highest version it
finds for a mod, so releasing one mod never touches the others.

- Raise `version` in the mod's `mod.json` for any change that should reach
  players, including a fix in `lib/` or `screen-host/` that the mod needs.
  Shared changes that do not matter to a mod wait for its next release.
- A published version is never replaced. A bad release is followed by a
  higher version.

      ./build.sh second-screen-balatro
      git tag second-screen-balatro/v0.1.1
      git push origin second-screen-balatro/v0.1.1
      gh release create second-screen-balatro/v0.1.1 --verify-tag \
          --title "second-screen-balatro 0.1.1" --notes "What changed" \
          dist/second-screen-balatro-0.1.1.tar.gz

Install and try the built package before tagging, and release that same
file: builds are not byte-for-byte repeatable.

## License

GPL-2.0-or-later, like Armada. See [LICENSE.md](LICENSE.md). Third-party
software in built packages keeps its own licenses.
