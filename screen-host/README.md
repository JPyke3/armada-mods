# Second-screen host

A ready-made program for a mod's `second_screen` entry. It shows pixels
streamed by the mod's code in the game, an HTML page from the mod, or the
page drawn over the pixels, and passes touch input back. `build.sh` copies it into each mod here as
`host/screen-host`.

It needs `python3-gobject`, `gtk4` and `webkitgtk6.0` from the OS.

## How a mod uses it

    "second_screen": "host/screen-host"

The mod's code inside the game (the provider) connects to `127.0.0.1:47800`.

The window takes the second screen as soon as the host starts, before the
game is up: a mod with `view/index.html` gets its page loaded at once, so the
page draws its own loading and idle screens, and a mod without one gets a
line reading "Loading" and the mod's name. The screen is given back (the
window is hidden) only when something is wrong: no provider has connected 90
seconds after the start, or a provider has gone and none has returned within
15 seconds. A provider can also ask for it with `"visible": false`, but a
title screen or a menu is not a reason to: show an idle screen instead.

Messages are little endian: `"A2SS"`, u16 type, u16 flags, u32 length, u32
sequence, then the body.

| Type | Direction | Body |
|---|---|---|
| 1 HELLO | provider to host | JSON with `appid` (one of the mod's `steam_appids`) |
| 2 FRAME | provider to host | u16 surface width, height, format (1), reserved, rect x, y, width, height, then RGBA rows bottom-up. The first frame must cover the whole surface |
| 3 STATE | provider to host | JSON object, delivered to the page in order. `"visible": false` hides the window. For a mod without a page, a `"message"` string is shown as one line in place of the pixels |
| 16 INPUT | host to provider | JSON. Pixel views: `{"t": "down"|"move"|"up", "x": 0..1, "y": 0..1}`, `{"t": "zoom", "s": scale}`, `{"t": "zoomend"}`. HTML views: whatever the page sent |
| 17 CONFIG | host to provider | JSON `{"w", "h", "fps"}`: surface size in pixels and the frame rate wanted, for FRAME messages |

## HTML views

A mod with `view/index.html` gets that page loaded when the host starts and
again whenever a provider goes away. The page is given `window.armada`:

- `armada.onState = function (state) { ... }` is called with each STATE, in
  the order they were sent.
- `armada.last` is the most recent state, or `null` while no provider has
  sent one: draw a loading or idle screen then.
- `armada.send(object)` sends an INPUT message to the provider. Nothing is
  listening until the first state has arrived.
- `armada.data` and `armada.game` are `file://` URLs of the mod's data folder
  and the game's install, for pictures a hook has prepared or the game's own
  files. Do not ship a game's art in a mod.

The page is drawn over the pixel surface and its background is transparent
unless the page paints one, so a provider can send FRAME messages as well and
the page decides where they show through. Touches go to the page; it passes
on the ones meant for the pixels as the pixel-view INPUT messages above.

The page can only navigate inside its own `view/` folder.

Size text from the screen, not in fixed pixels. A root font size of about
3.4% of the screen height gives readable body text on a small dense panel.

## Game runtimes without sockets

`filebridge <folder>` relays `hello.json` and `state.json` written into a
folder to the host, and writes input back as `input.json`. See
`second-screen-persona-3-reload/host/run` for how a mod starts both.

## Developing a view without the game

On the device, start the host by hand and attach the stand-in provider:

    armada-run-bottom -- ~/.local/share/armada-mods/mods/<id>/host/screen-host
    ./fake-provider <steam appid> [state.json ...]

It sends each state file, then any JSON lines typed on stdin, and prints what
the view sends back.
