# flipper-rat

Air mouse + presentation remote for Flipper Zero with the **Video Game Module** (its ICM-42688-P
motion sensor). Works over **Bluetooth** or **USB**. Builds for official firmware 1.4.3 and 1.5.x.

## Build / run

```bash
uv tool install ufbt      # once
ufbt                      # build -> dist/flipper_rat.fap
ufbt launch               # install + start on a USB-connected Flipper
make test                 # host unit tests (chords, key names, motion math, bundled profiles)
```

The SDK must match the Flipper's firmware API (a mismatch fails with `ApiTooNew`/`ApiTooOld`):

```bash
ufbt update --channel=release   # latest official (1.5.x)
ufbt update --hw-target=f7 --url=https://update.flipperzero.one/builds/firmware/1.4.3/flipper-z-f7-sdk-1.4.3.zip
```

On the Flipper: **Apps → GPIO → flipper-rat**.

## Controls

Pick **Start** in the menu. One screen does both: it's an air mouse, and Back/Up change slides
using the selected profile. Hold the Flipper still for a moment while it calibrates.

| Button | Action |
|---|---|
| hold **Right** | move the cursor (clutch). In *Always on* mode, Right pauses/resumes |
| **OK** | left click. Hold ~0.3 s, then move = drag |
| **Back** | next slide (profile `next`) |
| **Up** | previous slide (profile `prev`) |
| **Down** | right click |
| **Left** | laser pointer toggle (profile `pointer`) |
| hold **Back** | back to menu |

The cursor stays put while a click or slide button is down. Without the Video Game Module the
d-pad moves the cursor instead.

## Profiles

Text files in `SD:/apps_data/flipper_rat/profiles/`. Defaults are copied there on first launch
(an existing file with the same name is never overwritten), so edit or add files freely:

```
Filetype: flipper-rat profile
Version: 1
Name: Keynote
OS: macOS
next: RIGHT
prev: LEFT
pointer: none
```

Values are `MOD+KEY` chords. Modifiers: `CTRL`, `SHIFT`, `ALT`/`OPT`, `CMD`/`GUI`/`WIN`/`SUPER`.
Keys: `A`–`Z`, `0`–`9`, `F1`–`F12`, `LEFT RIGHT UP DOWN`, `PAGEUP PAGEDOWN HOME END`, `ENTER ESC
SPACE TAB BACKSPACE DELETE`, and `- = [ ] \ ; ' \` , . /`. `none` (or a missing key) does nothing.
Keys are sent as US-layout key positions.

Bundled: Generic, Keynote (macOS), Google Slides (macOS, Linux), PowerPoint (macOS, Windows),
Impress (Linux).

## Settings

Connection (Bluetooth/USB), sensitivity, acceleration curve, motion (clutch/always on),
grip (flat = screen up; upright = screen facing you, top edge up), left/right (normal/inverted),
up/down (*Point*: tilt up to move up, the default; *Flight*: tilt up to move down, like a joystick).
Stored in `SD:/apps_data/flipper_rat/settings.txt`.

Bluetooth shows up as **`Rat <your Flipper's name>`** with its own pairing store, so it doesn't
disturb the phone app or other remotes. *Forget BT pairings* in the menu clears it.

## License

GPL-3.0. The motion sensor driver and filter come from the Video Game Module Air Mouse in
[flipperdevices/flipperzero-good-faps](https://github.com/flipperdevices/flipperzero-good-faps).
