# oma-audio-books

A native, local audiobook library and player for Linux/Omarchy. C++20, Qt Quick,
SQLite, Qt Multimedia and ffprobe. No account, server, browser or network service.
MP3 book folders and standalone M4B files remain read-only.

![Audiobook library grouped by series, following the Omarchy theme](https://raw.githubusercontent.com/unTanana/oma-audio-books/main/docs/screenshots/library.png)

*Series view with the persistent player. Screenshots use a preview library and
illustrative listening progress; audiobooks are not included.*

<details>
<summary>More screenshots: chapters, playback, and light appearance</summary>

**Book details and chapter navigation**

![Dune book details, chapter navigation, and playback controls](https://raw.githubusercontent.com/unTanana/oma-audio-books/main/docs/screenshots/details.png)

**Light appearance with series headings hidden**

![Light appearance showing a continuous cover grid without series headings](https://raw.githubusercontent.com/unTanana/oma-audio-books/main/docs/screenshots/light-library.png)

</details>

## Install on Omarchy / Arch Linux

Build and install a local Arch package from source. `makepkg -si` installs the
declared dependencies, runs the tests, and installs the package; it will request
sudo access for package installation. Run it as your normal user.

```sh
sudo pacman -S --needed base-devel git
git clone https://github.com/unTanana/oma-audio-books.git
cd oma-audio-books
bash packaging/package.sh
cd dist
makepkg -si
```

Launch **oma-audio-books** from your app launcher, or run `oma-audio-books`.
Add your own audiobook folder in Settings. No books, account files, personal
catalog, or desktop configuration are included. The app reads your Omarchy theme
and uses your own XDG directories for its library and preferences.

## Build and run

Requires Qt **6.10 or later** (base, declarative/Quick Controls, multimedia, Wayland,
SVG), FFmpeg/ffprobe, CMake, Ninja, a C++20 compiler, and Qt's SQLite driver.
Python 3 is used only for the integration smoke. The tested host has Qt 6.11.2.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
./build/oma-audio-books
```

Use **Settings → Add folder** to select your library. The picker suggests
`~/Audiobooks`; no folder is imported without selection. Refresh rescans existing
roots. Each M4B is a book; MP3s in a folder form one book, with `Disc 1`, `Disc 2`
(or CD/Disk) subfolders supported. Consistent disc/track tags take priority over
natural filenames. Different album tags in one MP3 group are reported as ambiguous.
Separate unrelated books into separate folders. Directory/file symlinks are skipped.

Search title, author, narrator or series; filter favorites/completed/in-progress;
change sorting and square cover size. Details provide chapters, bookmarks,
corrections, cover selection, completion state and Relink. Relink accepts the same
relocated file, or a book folder with the same relative filenames and sizes; it
preserves identity, progress and bookmarks. It does not identify editions by title.
Remove a root to stop scanning it; catalog and personal state remain recoverable.

The library starts in **Series** view: each series has a heading with its book
covers underneath, in numeric order (1, 2, 2.5, 10). Series appear alphabetically.
The sort menu switches views and remembers your choice after reopening the app.
**Settings → Show series headings** switches between labeled sections and a
continuous cover grid. Hiding headings also removes row breaks between series;
book order stays the same. This choice survives restarts.
Cards show the series and book number.
Names match regardless of case or surrounding whitespace. Missing/unrecognized
numbers follow numbered books; books with no series go last, labeled **No series**.
Correct these fields under **Edit details → Series and order**.
The mouse wheel moves two cover rows per notch; precise touchpad scrolling keeps
its native movement. Progress saves update existing cards without rebuilding the grid.

Right-click a book for Play/Resume/Pause, favorites, completion, details, editing
and its containing folder. Unavailable books offer Relink instead of playback.
Finished books offer Play again from the first file. Mark unfinished keeps your
place; Start over in details asks before replacing the saved position and playing.
Bookmarks, corrections and speed are kept.

The bottom player stays active while browsing other books. Speed is per book;
remaining time accounts for speed. Previous/next navigate chapters or files;
±30 seconds crosses files. Sleep supports 15/30/60 minutes or the current chapter's
end. Restart restores the last selection paused. Closing saves and exits.
Dragging the seek bar previews the position and seeks once on release; arrow keys
commit on key release. Playback ticks do not move the thumb during a drag.

Keyboard: Ctrl+F search, arrows browse, Enter play/pause the highlighted book,
Ctrl+Enter open its details, Backspace/Escape return to the library, Tab through
controls. Space toggles the current player's playback across the main window;
holding Space or Enter does not toggle repeatedly. Search and other text inputs
retain normal Space entry and Backspace deletion,
and dialogs/menus keep their own key handling. Menu or Shift+F10 opens the focused
book's menu; arrows, Enter/Space and Escape operate it.

In the library, Ctrl+1 selects All, Ctrl+2 In progress, Ctrl+3 Finished and Ctrl+4
Favorites. Ctrl+Tab cycles forward; Ctrl+Shift+Tab cycles backward, wrapping at
either end. Alt+S opens sorting; use arrows and Enter to select. These browsing
shortcuts are inactive in text fields, dialogs, open menus and book details.
Filter choices keep the current search; sorting retains its saved preference.
Shortcuts are shown beside filter options and in the footer.

Appearance offers Follow Omarchy,
Dark and Light. Omarchy's state layout
is preferred, with config layout fallback; replaced palettes are reloaded.
MPRIS exports local playback controls, metadata, rate, volume, seek and position.
A per-catalog lock prevents concurrent playback; another launch requests focus.

## Verification

```sh
ctest --test-dir build --output-on-failure
```

Tests set `QT_NO_XDG_DESKTOP_PORTAL=1` to prevent Qt from starting desktop portals
on temporary D-Bus sessions. Native desktop checks must also forward it through
`hyprctl` launches; MPRIS and Wayland remain enabled.

The integration smoke generates clearly **synthetic** sine-wave MP3 and chaptered AAC/M4B
media, creates temporary XDG data/config/cache/state, exercises the actual scanner,
SQLite catalog and Qt FFmpeg media player, then launches a separate process to
verify restart state. It covers ordering, duplicate prevention, track transitions,
seek, chapters, speed, overrides, favorites, bookmarks, cover copying, filtering,
completion, cancellation, malformed media, disconnect/reconnect, removal/re-add,
relink, failed-load repair, chapter sleep, failed overlapping roots, artwork-cache
regeneration, Libation PART metadata, theme replacement/fallback and native QML
rendering. Menu checks cover target IDs across progress refreshes, removing a
favorite from a filtered grid, direct metadata editing, unavailable books,
keyboard handling, replay and confirmed/canceled Start over. Series checks cover
numeric/decimal order, normalized names, missing/invalid metadata, filters, ties,
section boundaries, cover resizing, keyboard navigation, saved view choice,
and selection/scroll/context-menu stability after sorting and refresh.
UI checks cover Omarchy, dark and light dropdown contrast, unclipped sort labels,
dropdown keyboard handling, heading visibility across two app launches, wheel
distance/bounds, precise scrolling, and card stability during playback saves.
It never reads a personal library. `--smoke` refuses unmarked/non-temporary state.
Two additional isolated checks cover scanner artwork/grouping/metadata bounds and
performance regressions with 1,000 books and 200 chapters per book. They assert
scoped updates, virtualized chapter navigation, stable delegates and zero catalog
writes or detail invalidations on unchanged scans. Mouse and keyboard seeking
commit once per release; malformed chapter metadata retains its file fallback.
Timings and allocated memory are reported without hardware-dependent limits.
Run `./build/performance-check 114` to measure a smaller synthetic collection.

The automated smoke sets `OMA_HEADLESS=1`: Qt's real decoded audio-buffer output is
used instead of connecting to desktop audio hardware. The actual application uses
QAudioOutput normally. No fake media player or mock catalog is used.
On Linux the app defaults to Qt's PulseAudio backend (also served by PipeWire),
which publishes `application.name=oma-audio-books` and supports desktop output
switching. An explicit `QT_AUDIO_BACKEND` override is preserved.

To run only
the backend probe **with real audio**, after generating/choosing your own local
MP3 and M4B test files:

```sh
./build/oma-audio-books --media-probe /absolute/test.mp3 /absolute/test.m4b
```

This plays briefly at low volume, seeks, resumes paused, changes speed and finishes
both sources. Headless decoding proves mechanics, not audible sound quality or
Audible compatibility.

## Local artifact and Arch packaging

Stage the versioned executable, icon, desktop entry and docs entirely inside this
project, without a system install:

```sh
cmake --install build --prefix "$PWD/stage/oma-audio-books-0.1.0"
python3 tests/smoke.py ./stage/oma-audio-books-0.1.0/bin/oma-audio-books
./stage/oma-audio-books-0.1.0/bin/oma-audio-books
bash packaging/package.sh
```

`dist/` contains the offline source archive and checksum-pinned `PKGBUILD`. To build
an Arch package without installing it or resolving dependencies as root:

```sh
cd dist
makepkg --cleanbuild --noconfirm
```

No `sudo`, `makepkg -s/-i`, account access, downloading, desktop configuration changes
or publishing is part of these commands. Install the resulting package later using
your normal package workflow if desired.

## State and preservation

Default paths (honoring XDG overrides):

- Data: `~/.local/share/oma-audio-books/oma-audio-books/library.sqlite` and `artwork/`.
- Cache: `~/.cache/oma-audio-books/oma-audio-books/covers/`.
- Preferences: `~/.config/oma-audio-books/oma-audio-books.conf`.

SQLite uses transactions, foreign keys and schema version 1. Personal corrections
are stored separately from imported metadata. Progress is saved every five seconds
while playing, on pause, seeking, selection changes and normal exit. A crash may
lose the last unsaved interval. Save failures appear in the window; normal close
is refused when progress cannot be saved. Source files are never tagged, renamed,
converted, moved or deleted by the app. Cached extracted artwork is regenerable;
chosen artwork is copied into application data. There is no catalog purge button.
Libation and existing system applications are unaffected. Sidecar parsing is deferred
until representative output is available.

## Acceptance status and limitations

The first-version implementation is complete. Release build, real synthetic
integration, and native offscreen grid/details rendering at 820×620 pass. The
single smoke includes regression checks for all six findings from the earlier
independent review; the finishing pass also reviewed startup, QML, MPRIS and
packaging. See IMPLEMENTATION.md for the recorded results and remaining limits.

A real Libation-exported MP3 was read using an isolated
temporary catalog: cover, author, narrator, series, PART order and chapters were
imported, and playback, pause, speed, seeking and chapter navigation were exercised
through a private D-Bus session. Its SHA-256 remained unchanged. Native Wayland
mapping and a second-instance handoff have also been observed. These desktop
checks run outside the restricted execution sandbox; the sandbox limitation is
not a limitation of the application.

- Human audible output/pitch quality, actual physical media-key use and visual
  confirmation of compositor focus requests remain unverified. Desktop checks
  use zero volume or decoded-buffer output; they do not prove audible output.
- Real M4B compatibility remains unverified; generated chaptered AAC/M4B passes.
  One real Libation MP3 is evidence for that file, not every Audible export.
  Sidecar schema support remains deferred.
- Synthetic performance checks cover up to 1,000 chapter-heavy books and 4,000
  cached scanner entries. Physical-wheel frame pacing, slow/network storage and a
  full screen-reader walkthrough remain unmeasured. Scans retain metadata in memory
  and probe new files serially; chapter and cover rows are virtualized. Bookmark
  rows are instantiated together. Old generated/custom artwork is not auto-pruned.
- MP3 grouping is intentionally conservative. Arbitrary directory hierarchies and
  renamed tracks need manual layout repair; Relink preserves relative layout and
  sizes, not arbitrary filename changes. Unknown durations display as unknown.
- Sleep chapter boundaries are checked against media position updates; sub-second
  overshoot is possible. No sample-accurate stopping is promised.

GPL-3.0-or-later; see LICENSE. Omakade inspired the desktop direction described in
the approved plan; no Omakade source or artwork was copied. The icon is original SVG.
