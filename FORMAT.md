# The mod format

A game mod is a folder of files that Armada applies to one or more Steam
games at launch, without changing the game's install. A mod can replace
or add files in the game's folder, generate files just before launch, set
environment variables and launch arguments, and run a program on the second
screen while the game runs.

## Where mods live

    ~/.local/share/armada-mods/
      mods/<id>/           the mod
      data/<id>/             the mod's own storage, kept across updates
      data/<id>/overlay/     if present, shown in the game folder like overlay/

Copy a folder into `mods/` to install a mod. Delete it, and its data
folder, to remove it.

To share a mod, pack its folder as `<id>-<version>.tar.gz` with the
folder as the only top-level entry, holding plain files and folders only.
Armada Store installs such an archive from Game Mods, "Add local .tar.gz",
lists it under Installed, and can remove it again. A folder copied in by
hand is listed too, but the store never replaces or removes it. A mod is
not installed, updated or removed while a game it applies to is running.

## The mod folder

    my-mod/
      mod.json
      icon.png               optional
      overlay/               files to show inside the game folder

Everything else is up to the mod. `mod.json`:

    {
      "id": "my-mod",
      "version": "1.0.0",
      "name": "My Mod",
      "summary": "One line for listings.",
      "description": "A longer text for the mod's own page.",
      "icon": "icon.png",
      "tags": ["dual-screen"],
      "steam_appids": [367520],
      "hooks": {
        "before_launch": "hooks/patch",
        "after_exit": "hooks/cleanup"
      },
      "second_screen": "screen/run",
      "env": { "WINEDLLOVERRIDES": "winhttp=n,b" },
      "launch_args": ["-nolauncher"]
    }

| Field | Required | Meaning |
|---|---|---|
| `id` | yes | Lowercase letters, digits and dashes. Must match the folder name |
| `version`, `name` | yes | |
| `summary` | no | One line for listings |
| `description` | no | Longer text about the mod. Listings do not show it yet |
| `icon` | no | A square PNG inside the mod folder, 256x256 or larger. Listings show the game's icon for now |
| `tags` | no | Lowercase words with dashes. `dual-screen` marks a mod made for devices with a second screen. Listings do not use them yet |
| `steam_appids` | yes | Steam app ids the mod applies to, as numbers |
| `hooks.before_launch` | no | Runs before the game, 10 minute limit. If it fails, the mod is left out of that launch |
| `hooks.after_exit` | no | Runs after the game exits, 60 second limit. Not guaranteed to run |
| `second_screen` | no | Program started on the second screen with the game and stopped when it exits. Only started on devices with a second screen |
| `env` | no | Variables set for the game. `{data}` expands to the mod's data folder. `WINEDLLOVERRIDES` is appended to an existing value |
| `launch_args` | no | Strings appended to the game's command line as they are |

Hook and `second_screen` paths are relative to the mod folder and must be
executable files inside it. They run with the mod folder as the working
directory and get:

| Variable | Value |
|---|---|
| `ARMADA_MOD_PACKAGE_DIR` | The mod folder |
| `ARMADA_MOD_DATA_DIR` | The mod's data folder |
| `ARMADA_MOD_GAME_DIR` | The game's real install folder, without any mod's files |

`env` is for the game only. A mod that lists several app ids shares one
data folder between them.

## How files reach the game

Files under `overlay/` in the mod, and under `overlay/` in its data
folder, appear at the same relative path inside the game folder for that
launch. A file with the same path as a game file replaces it.

To change a file the game ships, do not ship a modified copy. Have
`before_launch` read the file from `ARMADA_MOD_GAME_DIR` and write the
changed copy under `$ARMADA_MOD_DATA_DIR/overlay/`. It runs at every
launch, so a game update is picked up.

Rules to know:

- The game's own writes to its folder go to the real install as usual.
- Mod files are never copied into the install.
- A folder the mod adds is writable. What is written there stays in the
  mod: in its data folder if the folder exists there, otherwise in the
  mod folder, where an update of the mod discards it.
- Outside such folders, mod files are read-only and cannot be deleted,
  renamed or replaced while the game runs.
- Overlays hold plain files and folders, not links.
- Each file placed into a folder the game already has costs one mount, and
  about 2,600 are available per game. A new folder costs one however much
  it holds.
- Several mods can apply to one game, in id order. A later one wins when
  two provide the same file. Only the first with a `second_screen` program
  gets the screen.

What was applied or left out, and the output of hooks and the second-screen
program, is in `~/.cache/armada-mods/<appid>/mods.log`.

## The second screen

`second_screen` can be any program. It is started on the second screen's
display and stopped when the game exits. How it talks to the mod's code
inside the game is up to the mod. [screen-host](screen-host/README.md) is a
ready-made one that shows a web page, streamed pixels or both, and is what
the mods in this repository ship.

The second screen belongs to the mod from the moment the game is launched:
show a loading screen at once and an idle screen at the title and in menus,
and give the screen back only when something has gone wrong.

Do not repeat what the game's own display shows at all times, such as
health or money. Show what the player would otherwise have to open a menu
for.

Touch that acts on the game follows one rule. An action the player can undo
takes one tap: switching a mode, sorting, equipping, changing tabs. An
action that spends something takes two: the first tap selects and shows
what will happen with a clear button to do it, and the button does it. A tap
anywhere else, or a few seconds without one, cancels. A double tap is not a
confirmation.

Size text from the screen, not in fixed pixels, so it stays readable on a
small dense panel: a root font size of about 3.4% of the screen height
suits body text.

## Development

`touch ~/.local/share/armada-mods/dev` sets `ARMADA_MOD_DEV=1` and
`ARMADA_MOD_DEV_DIR` (`~/.cache/armada-mods/dev/<appid>`) for hooks,
second-screen programs and games, so a mod can enable its own debugging
and load development builds from that folder. Remove the file when done.
