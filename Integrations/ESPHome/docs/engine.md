# TAB-1 dashboard firmware

This firmware shows the dashboards you build in the Apollo TAB Dashboard
editor. Home Assistant sends each dashboard to the tablet while it runs, so a
new or changed dashboard needs no reflash. An admin picks which dashboard each
tablet shows.

The screen is landscape, 1280 × 800. Pages switch at once, with no animation.
It can hang either way up: see "Which way up".

## Files

| File | What it is |
|---|---|
| `TAB-1-engine.yaml` | The firmware for tablets in use. No secrets; it builds as is. |
| `TAB-1-engine-bench.yaml` | The same firmware with a fixed key from `secrets.yaml`, for the bench. |
| `Engine.yaml` | The dashboard engine package: actions, events, fonts, screen setup. |
| `engine_glue.h` | Connects the engine to the tablet: memory, flash storage, encryption key, events, erase. |
| `ota_lock.h` | Locks firmware updates to the tablet's own key once it has one. |
| `fast_display.h` | The fast screen path, for both ways up. Shared with `TAB-1.yaml` and the showcase. |
| `engine/` | The engine itself. Header files only; all of them go into the firmware. |
| `engine_tests/` | Tests that run on a computer, and their copy of the contract. Never part of the firmware. |
| `engine_tests/frozen/0.1.0/` | The sources of engine 0.1.0, frozen: the tests compare its pictures with this engine's. Never edited, never part of the firmware. |
| `fonts/Montserrat-Medium.ttf` | The text font, with its licence in `fonts/Montserrat-OFL.txt`. |
| `tools/gen_engine_fonts.py` | Writes the font list in `Engine.yaml` from the contract. |
| `tools/check_engine_package.py` | Checks the package against the contract and builds it. |
| `../../.github/workflows/build.yml` | Builds `TAB-1-engine.yaml` and publishes it for the tablets' update. |
| `../../.github/release-drafter.yml` | Release notes settings; the shared build workflow needs this file. |

The contract between the tablet and Home Assistant is
`engine_tests/contract/CONTRACT.md`. The same folder is copied, unchanged,
into the Apollo TAB Dashboard integration.

`TAB-1.yaml` (12 tiles) and `TAB-1-showcase.yaml` are not changed by the
engine. All of them use the same device name, so Wi-Fi settings and the Home
Assistant device carry over when you switch.

## Which file to flash

- **Tablets in use: `TAB-1-engine.yaml`.** No key and no password in the
  file. Home Assistant gives each tablet its own key the first time it
  connects, and the tablet keeps it. New releases come from Apollo through
  the tablet's "Firmware Update" in Home Assistant.
- **Bench work: `TAB-1-engine-bench.yaml`.** Same firmware, but the key comes
  from `secrets.yaml`, so you always know it. Use it when you need `esphome
  logs` over the network (opening the USB port restarts the chip), when you
  switch firmware often, or for the tablet's web page (log only, no firmware
  upload). It has no update channel.

Taps on the tablet only work with encryption. Without it the tablet still
shows its dashboard and values, but does not control anything.

## Before the first build

`TAB-1-engine.yaml` needs nothing.

`TAB-1-engine-bench.yaml` reads one secret from `secrets.yaml` next to the
YAML files. `secrets.yaml` is ignored by git; never commit it, and never
write the value into a YAML file.

```yaml
# TAB-1-engine-bench.yaml: the API key (it also encrypts its updates).
tab1_api_key: "<32 random bytes, base64>"
```

Make one with `openssl rand -base64 32`. Home Assistant asks for this key
when it adds a bench tablet. Older `tab1_ap_password` and
`tab1_ota_password` lines are no longer used.

## Setting up a new tablet

1. Flash `TAB-1-engine.yaml` over USB (or install it from ESPHome Web).
2. Within 15 minutes of power-on, join the open Wi-Fi "Apollo TAB1 Hotspot"
   from a phone and pick the home's Wi-Fi on the page that opens.
3. Add the tablet in Home Assistant when it shows up (Settings > Devices &
   services lists it by itself). Home Assistant gives it its own key on that
   first connection.
4. In TAB-1 in Home Assistant's sidebar an admin turns the new tablet on
   once, with its dashboard ("Turn on with this dashboard").

The waiting screen shows the tablet's name, where it is (a large headline)
and the next step (the line under it). It follows what Home Assistant does
right now, not only what the tablet stored:

| The tablet | Headline | Next step |
|---|---|---|
| Not on Wi-Fi, hotspot on | Set up Wi-Fi | Join the Wi-Fi “Apollo TAB1 Hotspot” from a phone (step 2) |
| Trying a Wi-Fi it knows, hotspot not on yet (up to 90 s) | Connecting to Wi-Fi… | If it cannot connect, its set-up hotspot comes on within 2 minutes |
| On Wi-Fi, no key yet, Home Assistant not connected to it (a new tablet) | Add this tablet in Home Assistant | It shows up by itself in Settings > Devices & services (step 3) |
| The same after the hold-to-erase, until it has a key again (see "Erasing a tablet") | Confirm this tablet in Home Assistant | In Settings > System > Repairs, confirm only “Authentication expired for Apollo TAB-1 12ee0e”<br>If this Home Assistant never had this tablet, add it in Settings > Devices & services |
| Has its key, Home Assistant connected, TAB-1 has not greeted it yet (Home Assistant still starting, or not turned on) | Waiting for TAB-1 in Home Assistant | If Home Assistant has finished starting, open TAB-1 there and turn this tablet on |
| Home Assistant connected, no greeting after 8 s (a tablet never turned on, or without a key) or 5 minutes (one turned on before) | Ready to turn on | Open TAB-1 in Home Assistant and turn this tablet on (step 4) |
| Has its key, Home Assistant not connected to it | Waiting for Home Assistant | Check that Home Assistant is running and lists this tablet in Settings > Devices & services |
| TAB-1 greeted it, a dashboard on its way (or 3 s not over yet) | Connected to Home Assistant | Getting the dashboard… |
| TAB-1 greeted it, nothing to show | Nothing to show yet | Open TAB-1 in Home Assistant to see what this tablet should show |
| The 15 minutes ran out without a key | Setup not finished | Restart the tablet to finish setup |
| The 15 minutes ran out with a key but no Wi-Fi | No Wi-Fi | Turn the tablet off and on, then join “Apollo TAB1 Hotspot” from a phone |

The tablet's name is the small line above the headline: "Apollo TAB-1" and
the last six characters of its MAC (here "Apollo TAB-1 12ee0e"). Home
Assistant gives the tablet that name when it adds it, so the erased screen
names the same question Repairs shows. The words of every screen are also
in the contract (`engine_tests/contract/screens.json`, CONTRACT.md section
11), which the TAB-1 panel's tests read.

"Home Assistant connected" means its own connection to the tablet, the one
that follows the tablet's states; its short look while it finds a new device
does not count. Once Home Assistant has started, TAB-1 greets the tablets an
admin turned on within about 3 s of that connection. While Home Assistant
starts, though, ESPHome connects to the tablet early and TAB-1 greets nobody
until its own start is done, which can take minutes. So a tablet with its
key says "Waiting for TAB-1 in Home Assistant" and "If Home Assistant has
finished starting, open TAB-1 there and turn this tablet on" for 8 s (a
tablet never turned on) or 5 minutes (one turned on before) before it says
"Ready to turn on" and "Open TAB-1 in Home Assistant and turn this tablet
on". A new tablet gets its key within the 8 s and does not show it. Once
TAB-1 greets it, the words change within a few milliseconds to "Connected to
Home Assistant" and "Getting the dashboard…", also while the dashboard is
still on its way.

"Nothing to show yet" and "Open TAB-1 in Home Assistant to see what this
tablet should show" match the panel's status for the same case. The step is
right whatever the reason: no dashboard made yet, none picked for this
tablet, or one too big for it; the panel says which. It comes as soon as
Home Assistant clears the tablet, or 3 s after TAB-1 greets it without
sending a dashboard.

If the 15 minutes run out before Home Assistant has added the tablet, turn
it off and on again; a new 15 minutes start.

If the hotspot does not show up, turn the tablet off and on again: it is
there for the first 15 minutes after power-on (after 90 s when the tablet
knows a Wi-Fi it cannot join).

### The set-up window

For 15 minutes after each power-on (`provisioning:` in the file) a tablet
can be set up:

- The hotspot is open, so anyone nearby can set its Wi-Fi in that time.
- A tablet without a key takes the first key it is offered. This is how
  ESPHome gives out keys; Home Assistant does it when it adds the tablet.
- Until the tablet has a key, firmware over the network and the hotspot's
  page are not locked yet.

After the 15 minutes, a tablet that still has no key turns Home Assistant
away until it is restarted, and its updates lock as below. Its waiting
screen says "Setup not finished" and "Restart the tablet to finish setup",
also over a dashboard it had stored. A tablet that already has its key works
as usual.

The hotspot also comes back after 90 s without Wi-Fi on a tablet that is
set up and has been on Wi-Fi since it was turned on, so a changed Wi-Fi
password can be fixed from a phone. Its page then changes only Wi-Fi: it
takes no firmware.

A tablet with its key that found no Wi-Fi in its first 15 minutes (a new
router or Wi-Fi password, or the router was off) keeps the hotspot off
until it is turned off and on again. It says so: "No Wi-Fi. Turn the tablet
off and on, then join “Apollo TAB1 Hotspot” from a phone", in a bar at the
bottom of its dashboard (a passing message shows over it for a moment), or,
on the waiting screen when it has no dashboard, "No Wi-Fi" and "Turn the
tablet off and on, then join “Apollo TAB1 Hotspot” from a phone". Turning it
off and on opens the 15-minute window and the hotspot again. If the old
Wi-Fi comes back instead, the tablet restarts by itself and the bar goes.

A tablet that loses its Wi-Fi later, with its dashboard on screen, says so
in the same bar while Home Assistant is not connected: after 20 s without
Wi-Fi "No Wi-Fi. Its set-up hotspot comes on within 2 minutes", and once
the hotspot is on (90 s) "No Wi-Fi. Join the Wi-Fi “Apollo TAB1 Hotspot”
from a phone". A tap then shows the same words. The bar goes when the Wi-Fi
is back.

## Updates

Firmware on the tablet can read the key that signs its touches, so new
firmware must come from whoever holds that key. Once Home Assistant has
given the tablet its key (or the set-up window closed without one):

- **Apollo's releases.** Home Assistant shows a "Firmware Update" for the
  tablet. The tablet fetches it only when Home Assistant asks, only from
  `https://apolloautomation.github.io/TAB-1/firmware/manifest.json`, and over
  HTTPS with the certificate checked. `.github/workflows/build.yml` builds
  and publishes it (raise `version:` in `TAB-1-engine.yaml` for a release).
  It builds with ESPHome 2026.10.2, not "stable": the lock below relies on
  ESPHome internals checked against that version. Raise it together with
  `min_version` in both engine files, and only after
  `tools/check_engine_package.py --host --config --compile` passes on the
  new version.
- **Over the network (`esphome run`, ESPHome Builder):** only updates
  encrypted with the tablet's own key get in. `ota_lock.h` gives plain
  updates a random password nobody knows. The bench file takes only updates
  encrypted with `tab1_api_key`.
- **The hotspot's set-up page** answers "Firmware updates need this tablet's
  API key" (403) to a firmware upload.
- **The tablet's web page** (bench file only) shows the log and takes no
  firmware (`web_server: ota: false`).
- **USB** always works; someone with the tablet in their hands can flash it.

The lock holds until the next restart. A tablet with a key locks again at
boot, before the update port opens, also in safe mode. When Home Assistant
removes the tablet it clears the key; the tablet restarts about 2 s later
and opens a new set-up window.

`tools/check_engine_package.py` fails if one of these doors is opened again,
or if a shared password comes back into a file.

### Publishing a release

`.github/workflows/build.yml` runs on each merge to `main` that changes a
YAML file (or by hand). It uses the shared Apollo build workflow, as the
other device repositories do. That needs two things once per repository:

- `.github/release-drafter.yml` on the default branch. The shared workflow
  drafts and publishes the GitHub release with release-drafter, which stops
  without it; then nothing reaches GitHub Pages.
- GitHub Pages turned on with Settings > Pages > Source set to "GitHub
  Actions". The workflow deploys `firmware/manifest.json` and the firmware
  there.

The web installer page (`index.html` at the repository root) is not
published with this firmware.

### ESPHome Builder

Adopting the tablet in ESPHome Builder ("Take control") writes a small file
that pulls in `TAB-1-engine.yaml` from GitHub and adds its own key. The
Builder needs ESPHome 2026.10.2 or later (`min_version`). The key must be
the one Home Assistant gave the tablet, or the Builder's update is refused.
If they differ, flash the Builder's file over USB, then paste its key in
Home Assistant when Home Assistant asks for it.

The Builder's firmware has its key built in. At each start it also writes
that key to the tablet's settings, where a key from Home Assistant would be
(`keep_key` in `ota_lock.h`). So when you later install Apollo's release
over it with "Firmware Update" in Home Assistant, the tablet starts with the
same key: Home Assistant stays connected and updates stay locked. That
install replaces the Builder's firmware, so changes you made in the
Builder's file are gone until you install from the Builder again.

Two things differ on a tablet that runs the Builder's firmware:

- Erasing it (below) removes Wi-Fi, the dashboard and the pairing, but not
  the key: it is part of the firmware and is written back at the next start.
- Home Assistant cannot clear the key when you delete the tablet. Before
  such a tablet leaves the engine or the home, erase it over USB ("Erase
  device" in ESPHome Web) and flash again.

## Erasing a tablet

Use this when Home Assistant asks for a key nobody has, before the tablet
moves to another home, or in a new home for a tablet that still has the old
home's key.

1. Turn the tablet on, or restart it.
2. As soon as the screen lights up, put a finger anywhere on it and hold it
   there for 5 s. The touch must start in the first 10 s after power-on, and
   the 5 s count from when the tablet's screen software has started (a few
   seconds after power-on), so put the finger on after the screen lights
   up, not before. The touch screen can miss the finger for a moment: up to
   three such gaps of at most 0.2 s each (0.6 s together) keep the count
   going, if the finger stays within about 60 pixels of where it went down.
   A longer lift, a fourth one or a finger that moves on starts the count
   over, so taps never add up to a hold.
3. The tablet asks "Erase this tablet? It forgets its Wi-Fi, Home Assistant
   and dashboard." Tap **Erase**. **Cancel** is the safe choice, and the
   question goes away by itself after 30 s.

The tablet forgets its Wi-Fi, its key, the stored dashboard and the pairing,
then restarts into the set-up window. The firmware stays. Give it Wi-Fi
again from the hotspot (step 2 of "Setting up a new tablet").

A key built into the firmware (ESPHome Builder, the bench file) is not
forgotten: it comes back at the next start. See "ESPHome Builder".

Then, within the 15 minutes, confirm the message Home Assistant shows for
the tablet: that it "disabled transport encryption" and whether to remove
the encryption key. Home Assistant lists it in Settings > System > Repairs
as "Authentication expired for" and the tablet's name (and in Settings >
Devices & services). Meanwhile the waiting screen says "Confirm this tablet
in Home Assistant", "In Settings > System > Repairs, confirm only
“Authentication expired for Apollo TAB-1 12ee0e”" and "If this Home
Assistant never had this tablet, add it in Settings > Devices & services",
with the tablet's own name. The erase leaves one mark in flash, nothing
secret, so the tablet knows it was erased. The mark goes once the tablet has
a key again.

Confirm only the question with this tablet's name (or the name you gave it
in Home Assistant), and only for a tablet you erased yourself. Never confirm
one for another device: any device on the network can make Home Assistant
ask such a question for a tablet that was never erased, and a yes gives
that device the tablet's key. In the home whose Home Assistant had the
tablet, do not add a new device by its name either: that could be another
one too.

The panel still has the tablet turned on, so the message above is all that
is needed. Home Assistant gives the tablet the key it kept for it, pairs it
again and sends the dashboard, and the approval stays; for a moment in
between the tablet says "Waiting for TAB-1 in Home Assistant" and "If Home
Assistant has finished starting, open TAB-1 there and turn this tablet on".

A Home Assistant that never had the tablet (a new home) shows no such
question: it lists the tablet as new in Settings > Devices & services, as
the screen's second line says. Add it there, as for a new tablet. Erasing
over USB leaves no mark, so such a tablet says "Add this tablet in Home
Assistant" and "It shows up by itself in Settings > Devices & services".

If the tablet's name is too long for the line (only possible with a
firmware you built with another `friendly_name`), the step says "In
Settings > System > Repairs, confirm only “Authentication expired” for the
name above" under "Confirm this tablet in Home Assistant", with the same
second line "If this Home Assistant never had this tablet, add it in
Settings > Devices & services".

If Home Assistant instead asks you to enter a key, close that dialog,
restart the tablet, and within 15 minutes reload the tablet in Home
Assistant (Settings > Devices & services > ESPHome, the tablet's menu,
Reload); then confirm the message above. Deleting the tablet in Home
Assistant and adding it again also works, but an admin must approve it
once more.

A touch that starts after the first 10 s never opens the question, and safe
mode has no erase. Erasing over USB (for example "Erase device" in ESPHome
Web, then flashing again) does the same and removes the firmware too.

## Build and flash

The first flash goes over USB. After that, tablets update through Home
Assistant; the bench file updates over Wi-Fi with its key.

```sh
esphome run TAB-1-engine-bench.yaml    # bench
esphome run TAB-1-engine.yaml          # tablets, over USB, or before the key
```

Pick your board in the `packages:` block of the file (Guition is the
default; the Waveshare line is next to it). Keep the two engine files the
same apart from the key, the `ota` block, the web page and the update
channel; the package check fails if they drift.

### Which way up

The dashboard is landscape, and the tablet can hang either way up.
`screen_rotation` in the `substitutions:` block of the entry file says which:

| `screen_rotation` | Picture |
|---|---|
| `"90"` | Landscape. The default from firmware 26.11.13.1 on. |
| `"270"` | The same picture turned by half a turn. Firmware up to 26.11.12.1 was always this way up. |

If the dashboard is upside down where your tablet hangs, set the other
value, then build and install the firmware again. Touch turns with the
picture: ESPHome turns every touch point the same way before the dashboard
sees it, so taps, swipes and the hold to erase need nothing else. Both ways
up use the fast screen path of `fast_display.h`, so pages switch as fast
either way. Keep the value the same in both engine files; the package check
fails if they differ or if the default is not `"90"`.

On a tablet adopted in ESPHome Builder, set it in your own file. It wins
over the package's value:

```yaml
substitutions:
  name: apollo-tab-1-a1b2c3
  screen_rotation: "270"
```

A tablet that takes Apollo's releases is always the default way up: the
value is built into the firmware. So the update from 26.11.12.1 or older to
26.11.13.1 or newer turns its picture by half a turn. If that is upside down
where the tablet hangs, turn the tablet, or adopt it in ESPHome Builder and
set `"270"` as above.

`"0"` and `"180"` are upright, 800 × 1280. ESPHome builds them, but
dashboards are made for 1280 × 800 and would be cut off, and
`fast_display.h` leaves them to ESPHome's own screen path. Any other value
stops the build with ESPHome's error for `rotation`.

`TAB-1.yaml` and `TAB-1-showcase.yaml` keep their own setting (270) and are
not changed by this.

On the tablet, after flashing (nothing here can be checked without one):

- The dashboard is upright for the way the tablet hangs, and fills the
  screen edge to edge with the side bar on the left.
- The log line `fast_display: CPU-rotate flush active (90 degrees,
  overlapped DMA, …)` shows at start (USB serial from power-on; network
  logs connect later). "keeping the stock flush" means the fast path is
  off.
- A tap on a card presses that card, also in the four corners of the screen.
- A swipe to the left shows the next page, a swipe to the right the one
  before, and a slider follows the finger in the same direction.
- A finger held on the screen in the first 10 s after power-on opens the
  erase question, and "Cancel" (on the right) closes it.
- Page switches look as fast as before (row 3 of the hardware checklist),
  with no stripes or torn bands while a page is drawn.

### Old copies of the package's files

ESPHome looks for each file under `includes:` in your ESPHome config folder
(the one with your device file and `secrets.yaml`) before the package's own
folder. An `engine/` folder, `engine_glue.h` or `ota_lock.h` left there from
an older copy of the package would be built instead of the package's, with
no warning. So each carries a version number (`TAB1_FILES_ENGINE` in
`engine/tab_files.h`, `TAB1_FILES_GLUE`, `TAB1_FILES_LOCK`), and
`Engine.yaml`, which always comes from the package, checks all three. A
copy with another number, or with none (made before the numbers), stops the
build with an error such as "ota_lock.h is not this package's copy: delete
the old ota_lock.h from your ESPHome config folder". Delete the file or
folder it names from the config folder and build again.

When you change any of these files, raise the three numbers and the three
checks in `tab_engine_start` (`Engine.yaml`, each an `#if` with an
`#error`) together, then record the files for the new number:

```sh
python3 -B tools/check_engine_package.py --record-files
```

It writes the SHA-256 of every file the number covers (all of `engine/` but
`tab_files.h`, then `engine_glue.h` and `ota_lock.h`) into `tab_files.h`,
under a `// files N all <sha256>` line that holds the SHA-256 of those
lines, and refuses while the number is the one already recorded. The
package check fails while the numbers differ, when a file differs from its
line at the same number ("raise it to 3 …"), and when the `all` hash is not
the one of the lines below it. Record once the files are final: a later
change needs the next number.

Two branches that each raise the number to the same N cannot both keep it:
their files differ. Their merge stops with a git conflict on the `// files N
all` line in `tab_files.h`. Do not pick either side: resolve it with any
text, raise the number again (N + 1, all four places), and run
`--record-files`. A merge resolved by picking one side fails the package
check, and `--record-files` refuses it, until the number is raised.

`fast_display.h` and `fonts/` are shared with `TAB-1.yaml` and carry no
number: a different copy in the config folder is built too, so keep those
the package's own. An older `fast_display.h` there has the fast path for
270 only: at 90 the picture is right, but the log says "keeping the stock
flush" and pages switch slower.

## Switching between the 12 tiles, the showcase and the engine

| Firmware | File | Encryption | What the tablet shows |
|---|---|---|---|
| 12 tiles | `TAB-1.yaml` | Off (a key can be added in the file) | 12 tiles you set up on the tablet |
| Showcase | `TAB-1-showcase.yaml` | Off | The swipeable demo pages |
| Dashboard engine | `TAB-1-engine.yaml` | On, key from Home Assistant | Dashboards from the editor |
| Dashboard engine, bench | `TAB-1-engine-bench.yaml` | On, key from `secrets.yaml` | Dashboards from the editor |

To switch, flash the other file. Into the engine, `esphome run <file>`
sends it over Wi-Fi or USB.

Out of the tablet engine (`TAB-1-engine.yaml`), first take Home
Assistant's key off the tablet:

1. While the tablet still runs the engine and is on Wi-Fi, delete it in Home
   Assistant (Settings > Devices & services > ESPHome, the tablet's menu,
   Delete). Home Assistant clears the key on the tablet and forgets its own
   copy; the tablet restarts about 2 s later without a key.
2. Flash the other file over USB, or with `esphome run` over Wi-Fi within 15
   minutes of that restart.

If the tablet cannot reach Home Assistant, erase it instead (with a finger
at power-on, see "Erasing a tablet", or "Erase device" in ESPHome Web over
USB), then flash the other file. Home Assistant keeps its copy of the key
then, but that copy never sat on the other firmware; back on the engine,
confirm its "disabled transport encryption" message as after an erase.

Why: the 12 tiles and the showcase take firmware over the network with no
password, also on their web page. With the key still in flash, someone on
the home network could install firmware that reads it. Home Assistant
would then use that same key again when the engine comes back (it keeps
every key it made, see below), and that person could install firmware on
the engine too.

Out of the bench engine, use USB: it takes network updates only encrypted
with its key, and the other files have no key (you can also add
`tab1_api_key` to the other file's `api: encryption: key:` first). Its key
is in the firmware and, since the firmware writes it there at each start, in
the tablet's settings too: `TAB-1-engine.yaml` flashed after the bench file
starts with `tab1_api_key` and locked. For a tablet without that key, use
"Erase device" in ESPHome Web first.

```sh
esphome run TAB-1.yaml                 # back to the 12 tiles (USB from the engine)
esphome run TAB-1-showcase.yaml        # the showcase (USB from the engine)
esphome run TAB-1-engine.yaml          # the dashboard engine
```

One firmware never holds two of these screens. Home Assistant tells them
apart by the engine's `tab_hello` action, not by a version number.

| From → to | What Home Assistant does | What you do |
|---|---|---|
| 12 tiles ↔ showcase | Nothing new | Nothing. |
| 12 tiles or showcase → bench engine | Asks for the key | Paste `tab1_api_key` from `secrets.yaml`. |
| Bench engine → 12 tiles or showcase | Says encryption was removed | Flash over USB; confirm once. |
| 12 tiles or showcase → tablet engine | Gives the tablet a key by itself and reloads (within the set-up window) | Nothing. |
| Tablet engine → 12 tiles or showcase | Clears the tablet's key and forgets it when you delete the tablet | Delete the tablet in Home Assistant while the engine runs, then flash. |
| Tablet engine again after that | Gives the tablet a new key (within the set-up window); the tablet waits for approval | Add the tablet in Home Assistant if it asks; an admin approves it again in the Apollo TAB Dashboard panel. |

While a tablet that is in Home Assistant runs the 12 tiles or the showcase,
the Tablets list in the Apollo TAB Dashboard panel says "Waiting for tablet
software". After the delete above, a tablet added to Home Assistant again
waits for an admin's approval; the panel suggests the dashboard it showed
before.

### What stays on the tablet when you switch

Flashing another firmware does not erase the tablet's settings. These stay in
flash while the 12 tiles or the showcase run, and the engine finds them
again when you flash it back:

- the last dashboard it showed, so it appears right after the flash;
- the pairing with the Apollo TAB Dashboard integration (replaced when an
  admin approves the tablet again);
- the encryption key Home Assistant gave the tablet engine, unless you
  deleted the tablet in Home Assistant first or erased it, as above;
- the key of a bench or ESPHome Builder firmware, until an erase over USB.

Erasing the tablet removes all of them.

Home Assistant (2026.11) keeps every key it made for a tablet, also after
you confirm "encryption was removed" and when you delete a tablet that is
switched off, erased or running other firmware. It forgets a key only when
you delete the tablet while the engine runs and Home Assistant can reach it.
When it meets the tablet again it tries that key first, or gives it to a
tablet without a key, so you are not asked. That is why the key comes off
the tablet before you leave the engine.

### When Home Assistant asks for a key you don't have

That happens only for a key Home Assistant did not make: one from ESPHome
Builder or from another Home Assistant. Do one of these:

- A key another Home Assistant gave the tablet (it runs
  `TAB-1-engine.yaml` as flashed): erase the tablet with a finger at
  power-on (see "Erasing a tablet"). It comes back with no key, and Home
  Assistant gives it one. The integration pairs again by itself.
- A key built into the firmware by ESPHome Builder: erasing does not remove
  it. Flash `TAB-1-engine.yaml` over USB after "Erase device" in ESPHome
  Web; the tablet then has no key and Home Assistant gives it one. Or paste
  the Builder's key in Home Assistant, if you have the Builder's file.
- Or flash `TAB-1-engine-bench.yaml` over USB. A key in the YAML file always
  wins over a stored one, so the tablet uses your `tab1_api_key`. Paste it in
  Home Assistant.

This is why the bench file has a fixed key.

## What the tablet shows

- **No dashboard yet:** the waiting screen with the tablet's name, a
  headline for the state it is in and the next step under it, in large
  white text (the table in "Setting up a new tablet"). The hotspot's name
  comes from `wifi: ap: ssid:` in the firmware. The tablet looks at its
  Wi-Fi at most twice a second while the waiting screen shows, over a
  dashboard only while Home Assistant is not connected, and never on the
  touch path.
  When the set-up window closes before the tablet has a key, it shows
  "Setup not finished" and "Restart the tablet to finish setup". The
  waiting screen then also covers a dashboard the tablet had stored (for
  example after Home Assistant removed it): Home Assistant cannot reach it
  until a restart. The stored copy stays for the next start.
- **No Wi-Fi for the whole set-up window** on a tablet with a key: "No
  Wi-Fi. Turn the tablet off and on, then join “Apollo TAB1 Hotspot” from a
  phone" stays in a bar at the bottom of the dashboard (on the waiting
  screen: "No Wi-Fi" and "Turn the tablet off and on, then join “Apollo TAB1
  Hotspot” from a phone") until the network is back, when the tablet
  restarts by itself. See "The set-up window".
- **Wi-Fi lost later** over a dashboard, while Home Assistant is not
  connected: after 20 s "No Wi-Fi. Its set-up hotspot comes on within 2
  minutes", then "No Wi-Fi. Join the Wi-Fi “Apollo TAB1 Hotspot” from a
  phone" in the same bar, until the Wi-Fi is back.
- **A finger held on the screen right after power-on:** the erase question
  (see "Erasing a tablet"), 5 s after the touch began, or after the engine
  started if the finger was there first. Swipes and taps on the dashboard
  wait while it shows.
- **After a restart:** the last dashboard, with empty values until Home
  Assistant sends them. It is built in the same small steps as a new
  dashboard, behind the waiting screen, so the tablet starts, answers Home
  Assistant and feeds its watchdog whatever the size of the dashboard: a
  usual dashboard is up in well under a second, the largest that can be
  stored (128 KB, about 10,000 parts) in a few seconds. If Home Assistant
  greets the tablet before that is done, it sends the dashboard itself.
- **A new dashboard:** it is built in the background while the old one stays
  on screen and keeps working, then shown in one step, never while a finger
  is on the screen (at most 3 s wait). A slider you are still dragging when
  the 3 s run out sends its value at that moment, as if you had let go; a
  slider that asks first shows "The dashboard changed. Try again" instead.
  The page you were on stays if the new dashboard has it.
- **Pages:** the side bar on the left, or a swipe left or right.
- **Home Assistant away:** the dashboard and its last values stay. After 10 s
  a "not connected" mark shows at the bottom of the side bar. A tap shows
  "Home Assistant is not connected" (or, without Wi-Fi, the Wi-Fi bar's
  words) and sends nothing. The tablet never restarts for lack of Home
  Assistant or Wi-Fi.
- **A tap that cannot be sent** shows why for a moment, and sends nothing:
  the Wi-Fi bar's words when the tablet has no Wi-Fi, "Home Assistant is not
  connected" (no connection, or Home Assistant has not greeted the tablet on
  this connection yet), "Touch is off: this tablet is not protected" (the
  tablet has no API key) or "Not paired with Home Assistant" (a key, but not
  paired yet). When only the greeting is missing, the tablet asks Home
  Assistant for it at once (a knock, at most one every 3 s), so the next tap
  works.
- **A control without a value yet** (right after a restart, before Home
  Assistant sends the values) is drawn faded. A tap on it says why, as
  above, while nothing can be sent; once the tablet can send, a faded
  control takes no taps until its value comes (there is nothing to change
  yet), and a swipe that starts on a faded slider switches pages. A button
  that switches pages always does.
- **Locks, alarms and garage, gate and door covers:** they follow the same
  rules as in the browser. The tablet asks first ("Are you sure?", the
  card's own question, or for a cover a question such as "Open Garage?"
  that Home Assistant adds). Home Assistant still checks every tap: it can
  answer that it needs a yes; then the tablet asks and sends again. A tap
  Home Assistant refuses shows "Not allowed on this tablet".

## How it talks to Home Assistant

Everything goes over the ESPHome connection Home Assistant already has.

- Eight actions that Home Assistant calls: `tab_hello`, `tab_status`,
  `tab_pair`, `tab_layout`, `tab_commit`, `tab_set`, `tab_ack`, `tab_clear`.
  The integration calls them directly, so nothing lands in the recorder.
- Two events the tablet sends: `esphome.tab1_hello` (a knock: "I am here and
  nobody greeted me", a few times after it connects, then rarely) and
  `esphome.tab1_input` (one signed tap).
- No sensors or other entities.
- Home Assistant can call `tab_hello` or `tab_status` a moment before it
  subscribes to states. The tablet remembers that call: if the subscription
  follows within 30 s, it counts as greeted, keeps the session and does not
  knock. Without such a call, a new connection starts a new session and the
  tablet knocks.
- Calls that change what a paired tablet shows are signed with a secret only
  the integration and the tablet know, so other Home Assistant users cannot
  change or blank the screen through the tablet's actions.
- Each `tab_hello` from Home Assistant starts a session with a new number
  (nonce). The tablet keeps the numbers of the last 16 sessions of this boot
  and starts no session with one of them again, so a recorded `tab_hello`
  cannot bring a recent session back. After a reboot, or 16 newer sessions,
  an old number works once more; recording one needs the encrypted ESPHome
  connection (contract 3). `tab_status` says whether a session is live;
  Home Assistant reads it when a knock arrives, so a knock from another
  device cannot reset the session.
- Without an API encryption key the tablet ignores a pairing secret it still
  holds: nothing is signed and no tap is sent until a key is back.

## What the tablet keeps in flash

- The last dashboard it showed, up to 128 KB, so it can show it right after a
  restart, before Home Assistant is back. It is written 30 s after a new
  dashboard appears, and only when it changed, so editing does not wear the
  flash. A write that fails (flash full, power glitch) is tried again 30 s
  later. A bigger dashboard still shows, but is not kept, and the tablet
  waits for Home Assistant after a restart. A damaged copy is thrown away.
- The pairing secret for the integration and a boot counter.
- The encryption key: the one from Home Assistant (production file), or a
  copy of the one built into a bench or ESPHome Builder firmware.
- ESPHome's own settings, Wi-Fi included.

Flash is written in the background, so the screen and taps never wait for it.
Erasing the tablet removes all of these: ESPHome's flash space `esphome`,
the engine's space `tab1`, then the whole settings partition.

## Checks

Run from `Integrations/ESPHome`. Use `python3 -B` so Python leaves no
`__pycache__` folder in `tools/`.

```sh
# Engine tests on this computer: protocol, drawing, memory, flash cache,
# and the screen rotation and flush of fast_display.h for both ways up.
# LVGL 9.5.0 is copied once from a local build, nothing is downloaded.
engine_tests/run.sh

# The same with all build output outside the repository
TAB_LVGL_DIR=/some/temp/lvgl TAB_BUILD_DIR=/some/temp/build \
  TAB_OUT_DIR=/some/temp/out engine_tests/run.sh

# The font list in Engine.yaml matches the contract
python3 -B tools/gen_engine_fonts.py --check

# After a contract change: write the font list again
python3 -B tools/gen_engine_fonts.py

# Package checks only (what CI runs): Python 3.12, standard library only
python3 -B tools/check_engine_package.py

# Also the flash queue and update lock tests on this computer
python3 -B tools/check_engine_package.py --host

# Also esphome config of the entry files and an adopted tablet, and a
# compile of the tablet file, the bench file and the adopted tablet
python3 -B tools/check_engine_package.py --host --config --compile --work /some/temp/folder
```

`engine_tests/run.sh` builds the tests twice (optimised, and with address
and undefined-behaviour checks), runs both, saves a screenshot of every page
of the test dashboards and lists texts that are cut with "…". Its default
folders (`engine_tests/.lvgl`, `.build`, `out`) are ignored by git.

It builds everything, LVGL too, with `-ffp-contract=off`: without it the
compiler fuses a multiply and an add into one step that rounds once, and a
colour worked out in doubles comes out one apart from the tablet's (whose
doubles are software) at a few values (test `doubles_are_not_fused`).

It also builds a second program, `tests/test_frozen.cpp`, from the sources
of engine 0.1.0 frozen in `engine_tests/frozen/0.1.0` (`engine/*.h`,
`host/harness.h` and `host/fake_platform.h` as at `dd3e379`, copied once,
never shipped), with the same compiler, flags, LVGL and fonts, and runs it
first, the same two ways. It draws every page of `layout_small` and
`layout_all_cards`, without values and after steps of `state_stream.json`
(`tests/frames.h`), and the engine's test `v1_pixels_unchanged` draws the
same and compares the frames byte for byte: a version 1 dashboard looks the
same on this engine as on 0.1.0. No hash of a frame is kept: the host draws
glyphs with floating point, so a stored hash would hold only for one
compiler and one CPU. A frame that differs is saved as
`v1_pixels_<frame>.png` among the screenshots. Never edit the frozen copy.

Card colours follow contract 4.7 in whole numbers: `engine/tab_colour.h`
holds `mix`, `lum`, `contrast`, `readable`, `legible`, `blend` and the nine
colours of a card, and `engine/tab_linear.h` the table `linear` of
`maths.json` (written once from the fixture; `lum` never calls `pow`).
A card's colours are worked out at its build and again only when its
`on`, `tint` or `un` changes, and a widget's own colours when its `c`
changes or its card's did; painting reads them and works nothing out
(test `maths_fixture` against `maths.json`, test `card_look_cache`). A
repaint changes only what differs (every style helper compares first), a
`tab_set` paints only the page on screen and marks the widgets of other
pages stale, which the timer paints a few at a time (never in a tick that
ran a build step) and `show_page` paints before the page shows (test
`repaint_cost`, which counts style calls and pending layout passes).

A slider of version 1 (the knob, no track) is four objects as before, laid
along its `dir`. A bar (`look` 1) and any slider with a `track` are one
object that draws its track, fill, handle, icon and mark itself; a new value
redraws only the strip between the old and the new place, with no style
call and no layout pass. Value from the finger and where a value is drawn
go through the helpers `slider_geom`, `slider_amount_at` and
`slider_shown`, checked against `maths.json` (`slider`, `track`). While it
is faded no part of it is drawn over another, so 40% is one picture (test
`slider_looks`).

It also builds and runs `tests/test_fast_display.cpp` the same two ways.
That test takes the rotation out of `fast_display.h` (with
`FAST_DISPLAY_HOST_TEST` defined, no LVGL and no ESPHome) and compares it,
for 90 and for 270, pixel by pixel with ESPHome's own software rotation,
copied into the test from ESPHome 2026.10.2: whole pictures, single pixels,
the first and last rows and columns, and areas that cross the tile and its
two halves, each drawn at the same place on a made-up panel, in bands from
the top down. It also checks ESPHome's touch rotation against it: a touch
on a drawn pixel is that pixel, both ways up. Copy the two ESPHome
functions again when the pinned ESPHome version changes.

The rotation test leaves out the part of `fast_display.h` that talks to the
tablet, so `tests/test_flush_cb.cpp` builds the whole file, as the firmware
does, against stand-in headers for ESPHome, ESP-IDF and LVGL
(`host/fast_display_sim`, one file for each header `fast_display.h`
includes). It runs the same two ways and plays LVGL and the panel. For 90
and for 270 it checks that `flush_cb` turns the picture the right way and
takes the panel's 800 for its width and 1280 for its height (the picture's
pixel (x, y) is the panel's pixel (799 − y, x) at 90 and (y, 1279 − x) at
270); that the bands go to the panel from the top down, each as large as
its part of the tile holds, in the two tile halves by turns when the copies
overlap and in the whole tile when they do not; that each copy is waited
for before the next one starts and before LVGL is told the flush is done,
that the next band is rotated while a copy runs (the overlap), and that a
copy which did not start is not waited for; that a tablet
without the panel handle or its semaphore draws through ESPHome's
`draw_pixels_at`; that nothing is drawn while LVGL is paused; and that
ESPHome's own flush keeps rotation 0 and 180 (also when the rotation
changes while the tablet runs), another resolution, another colour depth
and a tablet with no memory for the tile, each with the line `setup` logs.
When `fast_display.h` includes a new header, add its stand-in there. Look
at the stand-ins again when the pinned ESPHome version changes.

With no flags the package check runs only its static checks, and CI runs
it so: it needs Python 3.12 or later and nothing else (no PyYAML or other
package to install: it reads the YAML itself, the way PyYAML's safe loader
does, and refuses what it does not read), no network, no ESPHome and no
compiler. That run holds every check that needs no toolchain, the files-hash
guard among them, and the self-tests of its own rules (the YAML reader, the
files rule, the restart rule, the docs check, the compile warning gate).
Where PyYAML happens to be installed it also compares its reading of every
YAML file with PyYAML's. `--help` says the same.

The package check without ESPHome reads the entry files and fails when:
a shared password or a second secret is back (the tablet file has none, the
bench file only `tab1_api_key`); the hotspot gets a password or a timeout;
the set-up window is not 15 minutes; the update lock hooks are missing
(`tab_lock::boot`, `poll`, `safe_mode`); a key from the YAML is no longer
kept in the settings (`keep_key`); the update channel is not the HTTPS
address above with the certificate checked; a factory reset or safe mode
button is added; the two engine files differ in anything but the key,
`ota`, the web page and the update channel; `Engine.yaml` no longer takes
its `rotation` from `screen_rotation`, or an engine file sets another
default than `"90"`; `fast_display.h` loses its fast path for 90 or for
270, its fallback to ESPHome's flush, or its test in `engine_tests/run.sh`
(a self-test checks that rule too); or `engine_tests/run.sh` no longer
builds the whole of `fast_display.h` and runs `tests/test_flush_cb.cpp`
twice, that test leaves out `flush_cb`, `setup` or `set_enabled`, or a
header `fast_display.h` includes has no stand-in (with a self-test of that
rule as well); `engine_tests/run.sh` loses `-ffp-contract=off`, or no
longer builds the frozen engine 0.1.0 against its own `engine/` and runs it
twice before the engine with each result counted (self-test too); a font
of `Engine.yaml` differs from `contract.json` `fonts` in size, bits a
pixel or glyphs, or from `icons.json` up to level 2; or the metrics table
in `engine_tests/host/harness.h` does not follow the fonts of
`Engine.yaml`, or gives a digits font a line higher than its
`fonts.row_px` (self-test too). It also reads the erase (it
feeds the watchdog before each step and erases the partition once), the
lock's two restarts (each in its own case, run first in it: not commented
out or under `#if 0`, not under a condition, before any `break` or
`return`; a self-test checks that rule on made-up cases), its `#error` for
a build without the update password check, the erase mark (one byte,
written after the partition erase and before the restart, cleared once a
key comes), the glue's network and hotspot hooks for the waiting screen and
the Wi-Fi bar, that "Home Assistant connected" is a connection that follows
the states (`is_connected_with_state_subscription`), that this file quotes
every waiting screen with its exact headline and step (never one alone, cut
short or in old wording) and each Wi-Fi bar text whole, the files version
and the recorded SHA-256 of its files with their `all` hash ("Old copies of
the package's files"; a self-test checks two branches merged at one number),
and the release: `build.yml` publishes the tablet file with a pinned
ESPHome version equal to `min_version` in both engine files,
`.github/release-drafter.yml` is there, and `ci.yml` builds the tablet
file on that pinned version only, still builds `TAB-1.yaml`, and runs these
static checks. `--host` also builds the update lock's decisions on this computer and
tests them, runs the preprocessor over `ota_lock.h` for each file's update
settings and for settings it must refuse, and builds the headers with an
older copy of each, or one from before the numbers, in its place: each must
stop with the error that names it.

`--config` and `--compile` never touch this folder. They copy it, without
`engine_tests/`, to `--work` (a new temporary folder if you leave it out)
with throwaway secrets in a `secrets.yaml` there. The copy also gets
`adopt/tab1-adopted.yaml`, the file ESPHome Builder writes when it adopts a
tablet: it pulls in `TAB-1-engine.yaml` from a subfolder, so every include
must resolve beside that file, as for the package from GitHub. A second
one, `adopt/tab1-adopted-turned.yaml`, sets `screen_rotation: "270"` in its
own substitutions. `--config` reads the rotation ESPHome prints for each
file: 90 for the two engine files and the adoption, 270 for the turned
adoption, and 270 for `TAB-1.yaml` and `TAB-1-showcase.yaml`.

The compile builds the three files, each in its own data folder under
`--work`, and checks the build: `src/` holds the `engine/*.h` headers,
`engine_glue.h`, `ota_lock.h` and `fast_display.h`, nothing from
`engine_tests/` and no engine `.c` or `.cpp` file; the lock runs at boot, on
each loop and in safe mode (before the safe mode check); the build flags for
updates are the ones each file needs; the generated code sets the LVGL
rotation to 90, sets no update
password (`set_auth_password`), sets the 15-minute window, checks the
update server's certificate (`set_verify_ssl(true)` and the certificate
bundle in `sdkconfig`), has the safe mode hook (`USE_SAFE_MODE_CALLBACK`)
and Engine.yaml's three files-version checks with the same numbers; every
flash space the firmware opens is on the erase list; ESPHome still keeps a
key from Home Assistant in the settings place `keep_key` writes; the
font metrics equal the test harness table; and the host tests' `lv_conf.h`
has the tablet's LVGL settings (the generated `src/lv_conf.h`), apart from
the draw units, the default font, the allocator and the log. Any compiler warning from
`engine/`, `engine_glue.h`, `ota_lock.h` or a lambda of `Engine.yaml` or an
entry file fails the compile check (GCC names the YAML file and line of a
lambda); warnings from ESPHome's own code are listed, not failed. All of
that code builds in `main.cpp`, so the check deletes its object file before
each build and makes sure the log shows it built again. It reports firmware
size and RAM and flash use. Then it builds the adopted tablet twice more, as ESPHome
Builder does (relative path), with an `ota_lock.h` and then an
`engine_glue.h` from before the numbers beside its device file: each build
must stop with the error that names the file and says to delete it.

`--offline` blocks every download during those builds and reuses the files
in this folder's `.esphome` and the copy's build folder (components already
resolved there). ESP-IDF components that ESPHome's local component store
lacks must then come from a folder you name in
`IDF_COMPONENT_LOCAL_STORAGE_URL`.

### Size

Last full build: 2026-11-13, ESPHome 2026.10.2, Guition board. The tablet
file is larger than the bench file by the update channel (HTTPS client and
certificates).

| | `TAB-1-engine.yaml` | `TAB-1-engine-bench.yaml` | Adopted in ESPHome Builder |
|---|---|---|---|
| Firmware (app) | 2,111,424 bytes | 1,899,616 bytes | 2,108,384 bytes |
| Factory image (app + 64 KB boot and partition table) | 2,176,960 bytes | 1,965,152 bytes | 2,173,920 bytes |
| Internal RAM (static) | 111,728 of 576,464 bytes (19.4%) | 109,690 of 576,464 bytes (19.0%) | 111,720 of 576,464 bytes (19.4%) |
| PSRAM (static) | 2,029,476 bytes | 1,817,832 bytes | 2,026,436 bytes |
| Flash (app partition) | 2,111,020 of 8,126,464 bytes (26.0%) | 1,899,222 of 8,126,464 bytes (23.4%) | 2,107,980 of 8,126,464 bytes (25.9%) |

## Contract readings

The three cases the contract once left open are contract rules now, with
cases in `fixtures/chunks.json`: the thermostat "–" never uses the 48 px
digits font (`CONTRACT.md` 4.7), the order of `tab_layout` seq 0 header
errors, and a repeated commit of the hash being built (`CONTRACT.md` 5).
Tests: `reading_thermostat_dash`, `reading_seq0_header`,
`reading_repeated_commit` and `chunks_fixture` in
`engine_tests/tests/test_engine.cpp`.

Choices the engine makes inside the contract:

- **Numbers.** Contract 4.1 allows numbers from -1e9 to 1e9 with at most 3
  decimals. A number that is not finite (`1e400`) or whose text is 64
  characters or longer is refused: a layout with one answers `parse`, and
  `tab_set` skips that value. A finite number outside the range is held to
  -1e9 or 1e9, as Home Assistant's `num()` does. Test: `hostile_numbers`.
- **Slices.** A build slice makes at most 48 objects and a delete slice
  removes at most 96, one slice per 5 ms tick, so a card with many widgets is
  built and deleted over several slices. The delete slice's 4 ms count from
  the start of the tick, so a build slice and a delete slice together stay
  within 4 ms. When all 6 layout slots are taken (commits faster than old
  layouts are deleted), the oldest retired layout is deleted at once; full
  slots never answer `memory`. Tests: `slice_budget`,
  `delete_shares_tick_time`, `commit_burst`.
- **Session nonces.** The last 16 of this boot are kept in RAM, 256 bytes
  in all; the 17th session pushes out the oldest (contract 3). Test:
  `hello_nonce_ring`.
- **Chunks.** A `tab_layout` chunk over 16000 bytes answers `size` (contract
  5): at `seq` 0 before anything is allocated, later it drops the transfer.
  Test: `chunk_over_limit_is_size`.
- **A drag when a new dashboard is forced on.** After 3 s with a finger
  down the new dashboard is shown anyway. A slider drag in progress then
  ends as if released: its value is sent with the old dashboard's hash, or,
  for a slider that asks first, the strip says "The dashboard changed. Try
  again" (contract 7.4). Test: `forced_swap_ends_a_drag`.
- **Button text beside an icon** gets the room left by the glyph that is
  drawn, the fallback for an icon the font lacks. Test:
  `button_icon_room_uses_drawn_glyph`.
- **Rectangles.** A rectangle is cut to the 1280x800 screen (contract 4.3),
  so no widget is ever larger than the screen; a number in it outside
  -4000..4000 answers `parse`. The thermostat arc is also held to the size
  of an 800 px square: LVGL's radius mask writes past its buffer for radii
  above about 900 px. Test: `rects_cut_to_screen` (under ASan too).
- **Slots text.** Each character of `slots` is read once, escapes included,
  so a long escaped text costs one walk before it answers `limits`. The
  types of an escaped text are kept 3 bits a slot (385 bytes), which keeps
  the engine's fixed internal RAM under 2 KB. Test: `escaped_slots_are_fast`.
- **Start with a stored dashboard.** `tab::begin` only reads, checks and
  stages the stored copy; the 5 ms timer builds it in the usual slices and
  shows it. Building it inside `on_boot` would run before the main loop,
  with nothing feeding the task watchdog, and without LVGL's layout pass
  between steps the time grows with the square of the object count. Test:
  `boot_builds_in_slices` (9,900 objects: `begin` returns in a few ms).
- **A failed object.** When LVGL cannot make an object, the build stops with
  the status code `build`, whichever object it was, and the layout on
  screen stays. Test: `build_failure_is_reported` (fails each object in
  turn).
- **A touch without a session** (connected, key, paired) knocks at once, at
  most once per 3 s, and starts the knock schedule again (contract 2.2).
  Test: `touch_without_session_knocks`.
- **A faded control** (no value yet) stays clickable, so a tap while nothing
  can be sent shows why and knocks; while the tablet can send it takes no
  input, as before (contract 7.4). The paint functions no longer switch
  clickability; the touch handler reads one flag (`Widget.faded`). Test:
  `faded_controls_say_why` (a stored dashboard after a restart, Home
  Assistant connected without a greeting, then greeted without values, then
  away).
- **`tab_set` without an `h` text** answers `parse`, not `hash` (contract
  8). Test: `set_without_hash_is_parse`.
- **Thermostat value width.** `thermostatFit` counts 0.62 em a digit and
  Montserrat's digits are up to 0.67 em, so a number such as 4040 in a
  248 px gauge was cut at 48 px ("40…") although it fits at 34 px. The
  engine measures the text at the picked size and takes the next smaller
  size while it is too wide; only 14 px is cut (contract 4.7). Test:
  `thermostat_value_steps_down_when_too_wide` (every text of a list at every
  even size from 40 to 800 px).
- **Test fonts.** The host tests give the engine fonts that hold only the
  contract's characters (`fonts.text_glyphs`, `fonts.digits_glyphs`), like
  the tablet's, so a Greek letter is a box in the screenshots too.

## Hardware checklist

Nothing below can be tested without a tablet. Flash
`TAB-1-engine-bench.yaml` and measure over network logs (`esphome logs
TAB-1-engine-bench.yaml`); opening the USB port resets the chip.

Measurements:

| # | What | Target |
|---|---|---|
| 1 | Free internal RAM and PSRAM after boot with the engine, with the all-cards dashboard, and after 100 dashboard switches | Internal RAM within 2 KB of the showcase |
| 2 | Press to pixel time, also in the first 10 s after power-on (while the erase hold is watched) | 25 ms or less |
| 3 | Page switch time on the all-cards dashboard | 40 ms or less |
| 4 | One `tab_set` with 10 changed values: time on the main loop, including the `auth` check; and the `auth` check alone for a 12 KB call | 5 ms |
| 5 | Build time of the all-cards dashboard, and the longest single build step | No step over 6 ms; touch stays live during a build |
| 5b | Time from power-on to the stored dashboard on screen, for the all-cards dashboard and for one near 128 KB; the longest main-loop block during it | No watchdog reset; no block over 6 ms |
| 6 | `tab_commit` time for a 100 KB dashboard (hash and two parse passes) | Note it |
| 7 | Flash write of a 64 KB dashboard: time, and whether touch stutters while flash is written from PSRAM | No visible stutter |
| 8 | Where the API receive buffer lands for a 16 KB chunk (PSRAM or internal RAM) | PSRAM |
| 9 | Free PSRAM and largest block before and after a switch, to set the 1 MB margin | Note it |
| 10 | Longest step while an old dashboard is deleted page by page | Note it |
| 11 | Stack high-water mark of the 4 KB flash writer task during a 128 KB write | Comfortable headroom |
| 12 | Uptime when the engine starts: "Engine … started N ms after power-on" in the log (USB serial from power-on), cold start with no stored dashboard and with one near 128 KB. A hold must start before 10 s after power-on and only counts from this moment | Under 5 s, so at least 5 s are left to start the hold |
| 13 | Lowest free internal RAM during the production file's HTTPS update check, with the all-cards dashboard on screen: log `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)` and `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` (a temporary `interval:` lambda, read over USB serial: only Home Assistant has the production file's key) before and after "Check for updates" on its "Firmware Update" in Home Assistant, three times. TLS buffers come from internal RAM (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` is off) | Note it; 20 KB or more left at the lowest point. If thinner, try `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC: y` (`esp32: framework: sdkconfig_options`) and measure rows 2 and 13 again |

Checks:

- Boot with a stored dashboard, with none, and with a damaged one (the
  damaged one is thrown away and the waiting screen shows).
- Reconnect after a Home Assistant restart and after Wi-Fi loss. No restart
  after 20 minutes without Home Assistant.
- The tablet counts Home Assistant as connected only after Home Assistant
  subscribed to states (a log viewer does not count), and knocks only then.
  When the integration greets the tablet right after connecting, no
  `esphome.tab1_hello` event follows and taps work at once.
- Pairing on a tablet that got its key from Home Assistant on first connect
  (production file), including the ESPHome reload that follows. `tab_hello`
  must report `"enc":1` once the key is active.
- Signed taps end to end, including a lock with the question on the tablet
  and Home Assistant's own "needs a yes" answer.
- A call to `esphome.<name>_tab_clear` from Home Assistant's developer tools
  is refused on a paired tablet.
- `api.respond` works for all eight actions on the P4 build.
- Fonts: `·`, `°`, `–`, `ü` draw; a Greek or Cyrillic letter and an emoji
  draw as an empty box, as in the editor's tablet picture; an unknown icon
  draws the fallback (a question mark in a circle); the thermostat dash
  draws.
- A garage cover card asks "Open …?" on the tablet before it sends.
- Texts cut with "…": compare with the list from `engine_tests/run.sh` (the
  tablet's bitmap fonts may differ from the test fonts by a pixel).
- Arc and chart cost on a full page.
- Touch feel, including the 900 ms long press.
- The `build` code for `tab_status` (LVGL could not create an object during
  a build) is forced in the host tests; on the tablet, check that the old
  dashboard stays if it happens.
- Set-up window, production file: a new tablet shows the open hotspot, takes
  Wi-Fi from its page and gets its key when Home Assistant adds it. Without
  a key after 15 minutes Home Assistant is turned away, and a restart opens
  a new window. A set-up tablet whose Wi-Fi is gone brings the hotspot back
  after 90 s. A tablet with a key turned on with no Wi-Fi in reach shows no
  hotspot after 15 minutes; a power cycle brings the hotspot back;
  switching the old router on instead makes the tablet restart by itself.
- Waiting screen, production file: each headline and step exactly as below
  (and as in the host test pictures `waiting_*.png`), within a second of
  the change unless a time is given:
  - new tablet, not on Wi-Fi, hotspot on: "Set up Wi-Fi" and "Join the
    Wi-Fi “Apollo TAB1 Hotspot” from a phone";
  - a set-up tablet whose Wi-Fi is out of reach, until its hotspot comes
    on: "Connecting to Wi-Fi…" and "If it cannot connect, its set-up
    hotspot comes on within 2 minutes";
  - on Wi-Fi before Home Assistant adds it: "Add this tablet in Home
    Assistant" and "It shows up by itself in Settings > Devices & services",
    and Home Assistant lists it there by itself;
  - added, not turned on in TAB-1: "Waiting for TAB-1 in Home Assistant"
    and "If Home Assistant has finished starting, open TAB-1 there and turn
    this tablet on" for 8 s after Home Assistant connects, then "Ready to
    turn on" and "Open TAB-1 in Home Assistant and turn this tablet on";
  - turned on with "Turn on with this dashboard": "Connected to Home
    Assistant" and "Getting the dashboard…" at once, then its dashboard
    within a few seconds;
  - turned on, its dashboard cleared from TAB-1: "Nothing to show yet" and
    "Open TAB-1 in Home Assistant to see what this tablet should show";
  - Home Assistant stopped, on a turned-on tablet with no dashboard stored:
    "Waiting for Home Assistant" and "Check that Home Assistant is running
    and lists this tablet in Settings > Devices & services" (with a
    dashboard stored, the dashboard stays);
  - Home Assistant restarted (note how long it takes): "Waiting for TAB-1 in
    Home Assistant" and "If Home Assistant has finished starting, open TAB-1
    there and turn this tablet on" from its connection until TAB-1 greets
    the tablet, never "Ready to turn on" and "Open TAB-1 in Home Assistant
    and turn this tablet on" within 5 minutes; then "Connected
    to Home Assistant" and "Getting the dashboard…" and the dashboard;
  - an erased tablet (hold-to-erase) on Wi-Fi again: "Confirm this tablet in
    Home Assistant", "In Settings > System > Repairs, confirm only
    “Authentication expired for Apollo TAB-1 12ee0e”" (with its own name, the
    same as the line above the headline) and "If this Home Assistant never
    had this tablet, add it in Settings > Devices & services"; Home
    Assistant's Repairs lists "Authentication expired for" and the same
    name; after confirming, the mark is gone (the log says "the erase mark
    is cleared") and a restart shows no erase words; in a Home Assistant
    that never had the tablet it shows up in Settings > Devices & services;
  - no key after 15 minutes: "Setup not finished" and "Restart the tablet to
    finish setup";
  - a tablet with a key and no Wi-Fi for its first 15 minutes, without a
    dashboard: "No Wi-Fi" and "Turn the tablet off and on, then join “Apollo
    TAB1 Hotspot” from a phone"; with a dashboard, the bar over it says "No
    Wi-Fi. Turn the tablet off and on, then join “Apollo TAB1 Hotspot” from
    a phone";
  - a tablet showing its dashboard whose router is switched off: after 20 s
    the bar says "No Wi-Fi. Its set-up hotspot comes on within 2 minutes",
    after about 90 s "No Wi-Fi. Join the Wi-Fi “Apollo TAB1 Hotspot” from a
    phone", and the hotspot is there; a tap shows the same words; the bar
    goes when the router is back.
- Plaintext upload inside the window: on a new tablet (no key yet), a plain
  `esphome upload` and `curl -F update=@firmware.ota.bin
  http://192.168.4.1/update` on the hotspot page both work.
- Plaintext upload after the key: once Home Assistant has given the key, a
  plain `esphome upload` is refused (authentication fails); an upload
  encrypted with Home Assistant's key works (ESPHome Builder with that key).
  The same after a restart.
- Hotspot page after the lock (pull the router's power for 90 s so the
  hotspot comes back): `curl -F update=@firmware.ota.bin
  http://192.168.4.1/update` (multipart) and `curl -H 'Content-Type:
  application/octet-stream' --data-binary @firmware.ota.bin
  http://192.168.4.1/update` both answer 403 "Firmware updates need this
  tablet's API key", and the tablet keeps running its firmware.
- Safe mode refuses plaintext, for both kinds of key: force safe mode (ten
  resets in a row, each within a minute of power-on), then try a plain
  `esphome upload`. Refused on the
  production file with a key from Home Assistant, and on an adopted tablet
  with the key in its Builder file (`#elif defined(USE_OTA_ENCRYPTION)` in
  `ota_lock.h`). An upload encrypted with the key works.
- The boot log of a tablet with a key (USB serial, from power-on) shows
  "Updates locked to the API key (key in flash or firmware)" (production
  file, adopted tablet; the bench file says "Updates need the OTA
  encryption key (key in flash or firmware)") from
  `tab.lock` right after "Running through setup()" and before the first
  Wi-Fi line ("Connecting to …" or "Starting fallback AP"). The update port
  opens only after Wi-Fi is set up, so it never takes a plain upload first.
- Update lock, bench file: only uploads encrypted with `tab1_api_key` work;
  the web page and the hotspot page take no firmware.
- Removing the tablet in Home Assistant: the log says the key was cleared and
  the tablet restarts about 2 s later into a new set-up window. Without
  anyone adding it, that window closes after 15 minutes: the log says
  "Window expired", the waiting screen covers the old dashboard with
  "Setup not finished" and "Restart the tablet to finish setup", and a key
  pushed after that (Home
  Assistant adding the tablet) is refused until the next restart.
- "Firmware Update" in Home Assistant finds a release published by
  `.github/workflows/build.yml` on GitHub Pages and installs it over HTTPS:
  the P4 completes TLS to `apolloautomation.github.io` and its redirect,
  and checks the certificate. A bad certificate or another address is
  refused.
- Hold to erase: a finger put on as soon as the screen lights up and held
  opens the question 5 s after the later of the touch and the engine start
  (row 12), full screen, with Cancel lit. Letting go after the question
  appears taps nothing. Cancel, or 30 s without an answer, keeps
  everything. A touch that starts after 10 s never opens it. Also with a
  light or rolling finger: a hold whose touch reports drop out for a moment
  (up to 0.2 s) still opens the question; lifting the finger for longer
  starts the 5 s over.
- A finger already on the screen before power-on: note whether the touch
  controller reports it once the engine starts (some controllers take a
  finger present at their start-up as the empty screen). If it does not,
  only a fresh touch counts, as "Erasing a tablet" tells people to do.
- Erase: the tablet restarts with no Wi-Fi (also on the C6 Wi-Fi chip: it
  must not join the old network), no key, no stored dashboard and no
  pairing, and shows the hotspot. The log shows a clean restart, no
  watchdog reset, also with a 128 KB dashboard stored. After it has Wi-Fi
  again, confirming Home Assistant's "disabled transport encryption"
  message ("Authentication expired for" and the tablet's name in Repairs)
  within 15 minutes is enough: the same entry gets the tablet's key back, the integration pairs
  again and the dashboard returns, with no new approval. The boot log after
  the erase says "not paired, erased"; the log line "Could not leave the
  erase mark" must not appear.
- ESPHome Builder: adopting the tablet ("Take control") builds from GitHub;
  with Home Assistant's key in the Builder's file its update works, with
  another key it is refused, and the USB flash in "ESPHome Builder" above
  recovers it.
- Adopted tablet, then Apollo's release: on a tablet whose key exists only
  in the Builder's file (erase it over USB, flash the Builder's file over
  USB, paste its key in Home Assistant), the first boot logs "Kept this
  firmware's API key in the settings for later firmware" once, and not at
  the next boot. Install "Firmware Update" from Home Assistant: after the
  restart the log shows "Updates locked to the API key (key in flash or
  firmware)" before Wi-Fi, Home Assistant reconnects encrypted without
  asking anything, `tab_hello` reports `"enc":1`, and a plain `esphome
  upload` is refused.
- Hold to erase on a tablet that runs the Builder's firmware: Wi-Fi, the
  dashboard and the pairing go, the key is back after the restart.
- Swipe and tap while the tablet checks for an update (press "Check for
  updates" on its "Firmware Update" in Home Assistant, or make it rejoin
  Wi-Fi; it also checks every 6 h): the HTTPS request to
  `apolloautomation.github.io` runs in its own low-priority task. Note any
  stutter; press to pixel stays at 25 ms or less. Internal RAM: row 13.
- Every row of the switching table above, including leaving the engine
  after deleting the tablet in Home Assistant: the log shows the key
  cleared and a restart before the other file is flashed, the tablet's MAC
  is gone from Home Assistant's `.storage/esphome.encryption_keys`, and
  coming back gives the tablet a new key and asks for a new approval.

## Fonts

Text uses Montserrat Medium at 14, 15, 16, 18, 20, 28 and 34 px, with the
characters of `fonts.text_glyphs` in `contract.json`: Latin letters, accents
and common punctuation (including `·`, `–`, `…`, `°`, `€`). Any other
character, for example Greek, Cyrillic, Chinese or an emoji, shows as an
empty box. Big numbers use 48, 96 and 192 px with only the characters of
`fonts.digits_glyphs`: `0123456789.,-:°%+` and the space. The 96 and 192 px
ones are stored at 8 bits a pixel: ESPHome hands such a glyph to LVGL with
one copy instead of unpacking it half byte by half byte. Their lines are 59, 117 and 234 px
high, within the rows Home Assistant gives them (`fonts.row_px`: 60, 120,
240). The thermostat takes the 48 px digits only for `0123456789.,-`.
Icons come from Material Design Icons at 24, 28, 48 and 72 px: every icon
in `icons.json`, levels 1 and 2. An icon the tablet does not have shows as
a question mark in a circle.

The editor's tablet picture uses the same Montserrat Medium file, and draws
the same box for a character the tablet does not have, so texts are cut with
"…" at nearly the same place as on the tablet. Only the card in a normal
Home Assistant dashboard uses Home Assistant's own font.

Montserrat is under the SIL Open Font License 1.1 (`fonts/Montserrat-OFL.txt`).
