# oma-audio-books — implementation plan

Status: first-version implementation complete; remaining human acceptance limits
are listed below. This document records the implementation scope and verification.
See README.md for installation and use.

## Verified implementation and finish pass (2026-09-17)

- Release build with Qt 6.11.2 succeeds. The final source integration test passed
  in 9.46 seconds; the locally staged executable also passed the full smoke.
- The smoke uses real generated MP3/chaptered AAC/M4B, temporary SQLite/XDG state,
  the actual scanner and Qt FFmpeg backend. It covers import/restart, transitions,
  seeking, speed, corrections, bookmarks, filters, completion, removal/re-add,
  cancellation, malformed/missing media, relink conflicts, database write failure,
  theme changes, keyboard navigation and native QML rendering.
- The earlier independent review's six findings were reconciled against current
  code and runnable checks. Existing fixes cover chapter-sleep/EOF ordering,
  Previous at terminal EOF and partial disc tags. This pass fixed InvalidMedia
  retries, failed-parent/healthy-child scan deduplication, and cover regeneration
  including missing extraction history. Timed sleep now also retains the EOF stop
  guard. No review finding is knowingly left unresolved.
- Added Libation's PART series-order fallback. Imported titles and paths in
  tooltips now use plain text, like the main UI. No dependencies were added.
- Both the library and details screenshots at 820×620 were visually inspected,
  with light/dark palettes, artwork, missing artwork and an import-error banner.
  All persistent player controls remain inside the window; content scrolls.
- The prior independent parent checks used a real Libation MP3, a private D-Bus
  session and a mapped native Wayland window. This finishing pass repeats desktop
  checks against the staged release with isolated state and zero audio volume.
  The real file is read-only and its SHA-256 is checked before/after.
- The desktop entry passes desktop-file-validate and packaging scripts pass Bash
  syntax validation. Packaging uses local source and declared dependencies, with
  the integration smoke in makepkg's check() gate.

Remaining acceptance limits: human audible sound/pitch, physical media keys,
visual second-instance activation under the compositor, real M4B samples,
slow-storage/physical-scroll performance and a complete screen-reader walkthrough.
The latest full review was performed by the finishing agent; the separate reviewer
examined an earlier snapshot. No additional agents were started in this pass.

## Independent Ponytail follow-up (2026-09-17)

At the user's request, a fresh agent reviewed the current source, QML, tests and
packaging for unnecessary complexity. Three deletions were accepted: redundant
progress bookkeeping, an extra shutdown save connection, and an unread
second-instance socket payload. The reviewer verified the resulting diff and
found no further issues. Net source change: two fewer lines and no feature change.
The existing integration smoke passes.
This was a complexity review, not a new full correctness/security review.

## Space play/pause fix (2026-09-18)

The grid-only Space handler did not cover the rest of the window, and focused
Qt buttons consume Space before a normal shortcut. A window event filter now
handles unmodified Space before control dispatch. It preserves text input and
modal-dialog handling, ignores key repeat, and does nothing without a selected
book. The UI smoke reproduces the original failure and checks search/editor
spaces, grid/slider/details playback, repeat suppression and single activation
on the Play button. Release CTest passed: 1/1 in 8.94 seconds.

## Performance review (2026-09-18)

An independently spawned agent reviewed scanner, startup, desktop and memory costs;
the parent traced model, rendering, persistence and playback. Progress saves now
reload only their book, aggregate duration without probe JSON, and notify only the
relevant UI. Book lookup uses a native hash; row descriptors carry IDs instead of
whole metadata copies. Chapter buttons are virtualized and same-file jumps reuse
the loaded decoder. Probe-cache parsing runs in the scan worker; unchanged scans
skip redundant availability toggles, probe serialization and database writes.
Artwork is extracted only for the selected track when no existing cover wins.
Ancestor membership replaces quadratic grouping checks. Subprocess output limits
also apply when the child finishes quickly. No new dependency or schema migration.

A second fresh review removed unused probe parsing from playback track lookups;
chapter reads now extract only the chapter JSON through SQLite, with a malformed
metadata fallback. Sequential queries avoid Qt's result-row cache. Scans consume
their raw cache input, resolve imported metadata/artwork in the worker and discard
unchanged probes there. The GUI consumes the finished future result, and unchanged
scans skip catalog/detail invalidation. Seek dragging freezes the position binding
and commits once on release, preserving keyboard input without a timer or cache.

CTest and makepkg run the integration smoke plus isolated performance/scanner
checks. The latest source checks passed 3/3 in 13.14 seconds; clean package checks
passed 3/3 in 12.98 seconds, and the extracted package passed the full integration
smoke. Remaining acceptance limits are listed in README.md.

## 1. Objective and approved direction

Build a native, single-user, local-first audiobook library and player for Omarchy, inspired by Omakade's desktop integration and cover-focused interface. The application must work without a browser, Docker, an account, a server, or an always-running service.

The scope below is the approved implementation brief. The verification record above
distinguishes implemented behavior from acceptance items that still need human or
representative-media checks.

Working application/package name: `oma-audio-books`.
Default suggested library location: `~/Audiobooks` (respect the actual user's home directory).

## 2. Research baseline

Reference: https://github.com/btsouth/omakade
Inspected revision: `e5f296922f2c24a56085e9438503cd32d19ea9f6`.

Inspected Omakade's README, build configuration, theme implementation, selected QML, and library screenshot. Omakade uses C++20, Qt Quick/QML, CMake, local storage, and a standalone desktop launcher. It reads Omarchy colors and watches theme changes. Its theme implementation supports state/config directory layouts, resolves the monospace font, and queries Hyprland for visual metrics.

Borrow the desktop approach, not the game application. Do not import game scanners, achievements, controllers, cloud integrations, save protection, or a session daemon.

On the investigated host, Qt 6 base/declarative/multimedia/Wayland, FFmpeg, CMake, Ninja, and mpv were installed. The current theme palette was readable at `~/.local/state/omarchy/current/theme/colors.toml`. Treat these as discovery results, not portable assumptions; declare actual build/runtime dependencies in packaging.

This project is licensed under MIT. Omakade inspired the desktop direction; no Omakade source or artwork was copied.

## 3. First-version scope

### Library

- Add and remove configured local library roots through a folder picker.
- Suggest `~/Audiobooks`; never silently index the entire home directory.
- Import MP3 and M4B files, including chaptered M4B and multi-file MP3 books.
- Cover grid showing title, author, progress, and estimated remaining listening time.
- Adjustable cover size; preserve square audiobook art without imposing portrait game ratios.
- Search title, author, narrator, and series.
- All, In progress, Finished, and Favorites filters.
- Sort by title, author, recently added, recently listened, or series. Series mode
  keeps normalized series names together and orders books numerically, including
  decimals. Unknown order follows numbered books; no-series books go last.
  Series headings sit above rows of covers, with untagged books under No series.
  Hiding headings in Settings packs books into a continuous grid without row breaks
  between series, preserving book order. The toggle is saved immediately and
  restored on launch, defaulting to visible.
  Series is the initial view; the selected sort survives reopening. Cards show
  series/order in this mode; existing metadata corrections apply. Cover rows
  remain virtualized, with keyboard navigation and stable selection/scroll on refresh.
  Mouse-wheel steps scale to two cover rows, including pixel-bearing wheel events;
  phased touchpad scrolling remains native.
  Data-only refreshes update cards in place; changes to membership, order, sort
  or series headings rebuild the row layout.
- Themed dropdowns fit their longest option and use matching selected text/background
  colors. Open dropdowns retain their native Space handling. Tooltips, progress
  tracks and disabled controls use the application palette in every appearance mode.
- Book details: cover, title, author, narrator, series, description, duration, chapters, and Resume.
- Local metadata and cover corrections; corrections survive rescans.
- Explicit mark finished/unfinished actions and favorites; completion changes preserve position.
- Book context menu: targeted playback, favorites, completion, details/editing,
  containing folder and unavailable-book relink. Shared native Qt menu survives
  model resets and supports Menu/Shift+F10 plus standard menu navigation.
- Play again restarts a completed book at its first file. Separate Start over
  confirmation resets an unfinished book without removing bookmarks or speed.
- Clear empty, scanning, unavailable, and failed-import states.

### Playback

- Integrated persistent bottom player while navigating the library.
- Play/pause, seek, backward/forward skip, volume, and playback speed.
- Default skip interval: 30 seconds; avoid a preferences page solely for this value.
- Ordered transitions between book files and navigation between embedded chapters.
- Remember current book, track, offset, and playback speed across restarts.
- Restore the last selection paused; never autoplay merely because the app starts.
- Sleep timer with a small set of durations and an end-of-chapter option.
- Named bookmarks with chapter/file and position.
- MPRIS playback controls, metadata, position, and supported seeking/rate capabilities.
- One active playback session; launching the app again focuses the existing instance.
- Closing the window saves state and exits; no hidden background playback daemon in version one.

### Omarchy integration

- Follow the live Omarchy palette and desktop font.
- Read current state-directory theme files, with the legacy config-directory layout as fallback.
- Theme changes apply without restarting playback or losing UI state.
- Re-arm filesystem watches when a theme file or symlink target is replaced.
- Safe readable fallback when Omarchy is absent or its theme is invalid.
- Use appropriate spacing and rounding without modifying Hyprland settings.
- Keyboard-accessible navigation, visible focus, accessible names, and sensible minimum window size.
- Wayland-compatible native window, desktop entry, icon, and Arch package.

## 4. Deliberate exclusions

Do not add these speculatively:

- Remote streaming, phone apps, progress synchronization, multi-user accounts.
- Docker, HTTP API, web frontend, always-on backend, shell plugin, or tray daemon.
- Podcasts, ebooks, social features, ratings providers, recommendations, or cloud backup.
- Audible authentication, DRM implementation, payment, or store browsing.
- In-app download orchestration in the initial release.
- Automatic file renaming, moving, deletion, tag rewriting, or audio conversion.
- General plugin system, theme editor, generic persistence abstraction, or multiple playback backends.
- Recursive live filesystem watching in the first version; launch scan plus Refresh is sufficient.

These are scope boundaries, not excuses to omit explicitly listed first-version features.

## 5. Architecture: minimum complete application

Use C++20 with Qt 6 Quick/QML and CMake. Prefer native Qt facilities and existing host tools.

```text
QML library / details / persistent player
                 |
       Library model + Player + Theme
                 |
     SQLite catalog and user state
                 |
       Read-only local audio files

Background import --> ffprobe metadata/chapters
Cover extraction --> FFmpeg when necessary
Playback --> Qt Multimedia
Desktop controls --> Qt D-Bus / MPRIS
```

### Responsibilities

- Library: scanning, database access, catalog model, metadata overrides, filtering, and progress storage. Split scanner code only when its lifecycle warrants separation.
- Player: one QMediaPlayer/QAudioOutput, book-track sequencing, position persistence, speed, bookmarks/timer coordination, and error reporting.
- Theme: palette/font loading and live updates.
- MPRIS: narrow D-Bus adapter over the existing player, never a second state machine.
- QML: rendering, navigation, input, and binding to these objects; do not put SQL or subprocess orchestration in QML.

Use Qt's models/proxies, SQL driver, settings, standard paths, timers, file watchers, and process APIs. No ORM, dependency-injection framework, repository interface, event bus, or helper library with one consumer.

Start with a few cohesive implementation files and QML screens/components. Do not generate an elaborate folder hierarchy or a reusable design system before it is needed.

### Media backend gate

Use Qt Multimedia first. Before committing the application to it, prove MP3/M4B playback, seeking, resume, speed with pitch compensation, and file transitions on this machine.

Use ffprobe JSON for reliable duration, tags, streams, and chapter inspection. Extract embedded cover art with FFmpeg only when needed. Do not expect QMediaPlayer's media-track APIs to provide an audiobook chapter model.

If the real playback gate fails, report the exact failure and evaluate mpv as the alternative. Choose one backend; do not build a pluggable backend abstraction or silently reduce supported behavior.

## 6. Import rules and preservation guarantees

### Supported layout

```text
Audiobooks/
  Author/
    Book One/
      cover.jpg
      01.mp3
      02.mp3
    Book Two/
      Disc 1/...
      Disc 2/...
    Book Three.m4b
```

- A standalone M4B is a book.
- MP3 files in one book folder are one book; recognize disc subfolders within that book.
- Do not merge all files recursively under an author or library root.
- Multiple independently titled M4Bs remain separate books unless an explicit supported grouping rule proves otherwise.
- Use disc/track tags when complete and consistent; otherwise natural filename order with a stable path tie-breaker.
- Inconsistent or ambiguous layouts are reported with a path and actionable explanation instead of guessed into the wrong book.
- Overlapping roots and repeated scans must not duplicate the same canonical files.
- Do not follow directory symlinks into cycles or out of the selected root by default.

### Metadata precedence

Local user correction wins over imported metadata. Initially prefer embedded tags, then deterministic filename/folder fallbacks. Prefer a user-selected cover, then conventional adjacent cover art, then embedded art, then a neutral placeholder.

Support title, author, narrator, series/order, description, duration, and chapter names when present. Missing metadata is not a fatal import error. Validate Libation sidecar formats using actual downloaded output before adding a parser; do not invent its schema or require a sidecar for playback.

### Incremental scanning

- Scan on launch and explicit Refresh without blocking UI or audio.
- Probe new/changed media based on stored path, size, and modification time.
- Bound probe concurrency, give subprocesses timeouts, and support cancellation.
- Keep subprocess arguments as separate arguments; never construct shell commands from media names.
- Only reconcile missing entries after a successful scan of an available root.
- Failed or canceled scans do not erase catalog entries.
- Disconnected storage marks entries unavailable and retains progress, favorites, bookmarks, and corrections.
- A missing individual file produces a repairable unavailable entry, not destructive cleanup.
- Version one need not automatically recognize arbitrary renamed/moved files. Provide an explicit relink flow that preserves the existing book identity and state; do not match different editions by title alone.

Removing a root removes its active scan configuration, not the original files. Preserve personal state and cached entries as unavailable so re-adding or relinking can recover them. Any future permanent catalog purge must be explicit and separately confirmed.

## 7. Data and filesystem design

Use QStandardPaths/XDG locations. Keep the catalog separate from audio:

- Data: application SQLite database and user-selected artwork copies.
- Cache: generated thumbnails/extracted artwork; safe to regenerate.
- Config: small QSettings preferences such as roots, grid size, and window state.

Minimum conceptual tables:

- books: stable internal ID, imported metadata, availability, favorite/completion state, and last-listened time.
- tracks: book ID, source path, ordering, duration, size/mtime, and probe metadata.
- chapters: book/track ID, start/end offsets, and title.
- progress: book ID, track ID, offset, and playback speed.
- bookmarks: book ID, track ID, offset, and label.
- overrides: local metadata corrections where a separate table is simpler than nullable override columns.

This is a conceptual model, not a mandate for a table per bullet. Use the smallest schema that cleanly separates imported values from personal state.

Use transactions and a schema version. Preserve user state during metadata refresh. Serialize database writes on one owner thread; background probes return results rather than sharing a Qt SQL connection across threads. Handle full disk and write errors explicitly. Never claim progress was saved if persistence failed.

Save progress periodically (initial target: every five seconds), on pause, book/track changes, and normal exit. Capture the outgoing track position before replacing media. Resume only after the selected file has loaded and is seekable; clamp positions to valid duration. A crash may lose the latest unsaved interval, but must not corrupt committed state.

Calculate whole-book progress from ordered track durations plus the current track offset. Remaining listening time reflects the selected speed. If durations are unknown, show an honest unknown/loading state rather than a fabricated estimate.

## 8. UI outline

One main window:

1. Compact top toolbar: library name, search, Refresh, and Settings.
2. Filter row: All / In progress / Finished / Favorites, sort, and cover size.
3. Scrollable cover grid with stable focus/selection across refresh and filtering.
4. Book details view with metadata, chapter list, bookmarks, and Resume.
5. Persistent player along the bottom; opening another book's details does not switch playback.

Settings stays small: library roots and necessary playback/app preferences only. Metadata corrections belong to book details.

Keyboard baseline: Ctrl+F focuses search; arrows navigate the grid; Enter plays/pauses the highlighted book; Ctrl+Enter opens details; Backspace or Escape returns to the library; Tab reaches controls. Space controls the current player's playback outside text-entry contexts. Backspace retains deletion in text inputs and does not navigate behind dialogs. Held Enter/Space does not repeatedly toggle playback. MPRIS handles global media keys. No global Hyprland binding changes without separate approval.

Library browsing shortcuts use native Qt Shortcuts: Ctrl+1–4 selects All/In progress/
Finished/Favorites, Ctrl+Tab and Ctrl+Shift+Tab cycle with wrapping, and Alt+S opens
the existing sort popup for arrow/Enter selection. They reuse the dropdown activation
paths, preserve search and sort persistence, and are disabled in text fields,
dialogs/popups and details. Filter changes focus the book grid; held shortcuts do
not cycle repeatedly. Labels/tooltips and the footer expose the shortcuts.

Render imported titles/descriptions as plain text. Avoid untrusted rich text, remote cover fetches, and hidden network requests.

## 9. Audible acquisition boundary

Libation remains responsible for Audible sign-in and downloads. The library consumes its completed output in `~/Audiobooks` or another selected root.

The installed Libation CLI was verified to expose scan and selected-book liberate commands. A future integrated workflow would need user-controlled authentication, settings discovery, progress, cancellation, concurrency, and failure handling. It is not part of the initial implementation.

Do not collect credentials, parse private account files, or begin downloads as part of import. Leave existing audiobook applications and their data untouched.

## 10. Delivery phases

### Phase 1 — media feasibility

- Build the smallest disposable Qt playback probe.
- Exercise MP3 and M4B, embedded chapters through ffprobe, seeking, speed/pitch behavior, saved-position resume, and sequential playback.
- Use generated media only for deterministic mechanics; label it as generated.
- Obtain representative user-owned downloaded books before final compatibility acceptance.

Exit gate: real backend results recorded; choose Qt Multimedia or report a concrete blocker before proceeding.

### Phase 2 — working vertical slice

- Minimal build target and native window.
- Theme adapter with live updates and fallback.
- Add a folder, scan into SQLite, and display a cover grid.
- Open a book, play/pause/seek, transition tracks, and restore paused progress after restart.
- Keep scan/database failures visible and preserve source files.

Exit gate: add folder -> import -> browse -> play -> restart -> resume works end to end.

### Phase 3 — complete first-version behavior

- Search, filters, sorting, favorites, and completion state.
- Details, metadata/cover corrections, chapter navigation, and bookmarks.
- Sleep timer, per-book speed, MPRIS, and single-instance focus.
- Missing-root and relink behavior, incremental refresh, and accessible keyboard navigation.
- Validate light/dark themes, image-less entries, long metadata, and minimum window size.

Exit gate: every scoped feature is present; no stubbed buttons or success-only error handling.

### Phase 4 — verification and packaging

- Add desktop entry, icon, Arch PKGBUILD, and declared dependencies.
- Build/install a versioned local artifact without overwriting unrelated apps.
- Verify actual installed executable and mapped Wayland window.
- Run the integration smoke against the final artifact and perform desktop checks.
- Record run/build commands, results, limitations, and data paths in README.
- Obtain a focused independent correctness/security/Ponytail review of the full change, including packaging and new files; fix accepted findings and rerun checks.

Exit gate: installed artifact exercised; no unresolved data-loss defects; incomplete acceptance items explicitly reported.

## 11. Verification strategy

Prefer one small real integration smoke with temporary XDG directories and a temporary media library over numerous mocked unit suites. Never point automated tests at the user's live database or audio collection.

The smoke should exercise:

- Import single-file M4B and ordered multi-file MP3 without duplicates on rescan.
- Chapter/file transitions and seek-to-position behavior through the real media backend.
- Persist and recover progress, bookmarks, speed, favorites, and metadata overrides across a real restart.
- Rescan modified metadata without losing personal state.
- Missing/reconnected roots and malformed media without deletion or a crash.

Use a few focused assertions for ordering or parser edge cases only when the integration path cannot expose them clearly. Avoid per-function tests, source-text assertions, and artificial mock coverage.

Manual desktop acceptance:

- Audible sound, seek accuracy, and pitch-preserving speed on representative books.
- Play/pause/next/previous/seek through MPRIS and actual media keys.
- Theme update without restarting the app or playback.
- Keyboard-only browsing, details, search, editing, and playback.
- Sleep timer and end-of-book behavior.
- Closing/reopening and second-instance behavior.
- Responsive scrolling/scanning with a representative collection; measure before adding optimizations.

Real-MP3 checks used a Libation export without relocating or modifying it.
Generated M4B success does not establish real M4B compatibility.

## 12. Ponytail rules and approval gates

- Read and reuse local code before adding abstractions.
- Use Qt/SQLite/FFmpeg rather than inventing substitutes.
- Keep one application, one player, one catalog, and one owner for persistent state.
- No service, online integration, framework, or configuration surface solely for future use.
- Keep original media read-only and preserve personal state; these safeguards are not optional complexity.
- Add concise `ponytail:` comments only for genuine known ceilings and their upgrade conditions.
- Report observed results, not intended results. A successful compile is not playback or desktop verification.
- Plan changes that expand scope or introduce a replacement backend must be reported before substantial implementation.
- No installation cleanup, remote publication, credential handling, or unrelated system configuration without separate authorization.

## 13. Definition of done

The first version is done only when the native installed app imports supported local books, presents the Omarchy-themed library, plays and resumes them correctly, implements all listed first-version controls, survives rescans/disconnections without losing personal state, passes its real smoke, and has representative-media/desktop results recorded.

Any remaining unsupported layout, codec, packaging issue, or untested real-media requirement must appear in the final report. Do not silently narrow this definition to the happy path.
