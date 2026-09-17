# Wine-NX GameHub-style launcher implementation plan

Status: implementation baseline completed. The explicit catalog, Add Game workflow,
Home/Library/Details navigation, filtering, favorites, sorting, migration, and host
coverage are implemented. Custom landscape/square PNG selection and the equivalent
interactive Add Game picker in the emergency console fallback remain follow-up work.

## Product decisions

Keep SDL2, SDL2_ttf, and the existing C launcher. Build a GameSir GameHub-inspired interface with Wine-NX branding and a controller-first layout.

Required behavior:

- Home uses an animated horizontal carousel of upright 2:3 rectangular covers, following the user's screenshot. The earlier landscape-card assumption is superseded.
- Every game-library view uses square 1:1 artwork cards, including filtered and search results. Titles sit below the square artwork.
- Games enter the library only through **Add Game**, where the user selects one executable in a file browser.
- Opening the launcher never discovers or imports executables by scanning game directories.
- Adding a game saves its library entry; it does not launch the executable.
- Both graphical and console launchers follow the same explicit library membership rules.

Design reference: [GameSir's GameHub listing](https://gamesir.com/support/downloads) and the [publisher's GameHub app listing and screenshots](https://play.google.com/store/apps/details?id=com.xiaoji.egggame). The layout, measurements, colors, and behavior below are proposed Wine-NX decisions, not a claim of exact parity with a particular GameHub release.

## Screens and navigation

Use a persistent header with **Home**, **Library**, **Add Game**, and **Settings**, plus contextual controller hints at the bottom. Start on Home; preserve focus and scroll position when returning from another screen.

### Home

- Dark charcoal background, subdued surfaces, bright text, and a fixed neutral accent. The legacy selectable theme system is removed.
- A larger focused cover stays at the left; smaller neighboring covers slide through the same row. The selected title and View Details action sit below the row, with selected artwork dimmed across the backdrop.
- Controller arrows and horizontal swipes move one game. Tapping a neighbor focuses it; tapping the focused card or View Details opens its menu. Drawing and touch share the animated card rectangles.
- Covers have rounded corners and a thin, bright outline around the selection. Focus scales and scrolling ease together; disabling animations snaps directly to the selection.
- A grid button opens Library. Add Game remains available in the header even with a populated library.
- Empty state: “Add your first game” and a prominent Add Game button. Explain that the user chooses the game's `.exe` from the SD card.
- All Home game cards, including any later shelves, remain rectangular.

1280×720 layout: covers align at y=126; the focused cover is 216×324 at x=96, and neighboring covers are 168×252 with 24 px gaps. Previous/next cards clip at the viewport edges. The title starts at x=340 beneath the smaller covers; View Details sits below the focused cover. Eight-game host fixtures verify the first, middle, and last selection, taps, swipes, and square Library.

### Library

- **All Games** as the default view, with **Favorites** as a filter and title search through the existing platform keyboard prompt.
- Sort options: title, recently added, and recent launch request. Use title as the deterministic secondary sort and stable entry ID as the final tie-breaker.
- Square artwork, title below, optional favorite and missing-file indicators. Initial density: five columns and two visible rows at 1280×720, with approximately 192×192 px artwork and a 32 px caption area.
- Vertical scrolling, with partial final rows handled predictably. Scrolling and drawing share the same layout rectangles used for touch hit testing.
- Empty library, no search results, and all games hidden have distinct messages and appropriate actions.
- Existing hidden games stay hidden by default; provide “Show hidden games” in Settings. An explicitly re-added hidden entry can be unhidden from its details.
- A future collection uses this same square-card renderer rather than introducing another card shape.

### Game Details

Selecting a card opens Details. Show artwork and title with **Play** as the initial focused action, followed by Favorite, Edit, Game Settings, and Remove from Library.

- Edit contains title, landscape artwork, square artwork, and Locate EXE.
- Game Settings exposes the existing arguments, rendering options, traces, profiler, controls, and Box64 information, respecting current runtime capabilities.
- Remove from Library confirms the action and removes membership only. It leaves the executable, game files, and existing per-game settings intact.
- Missing executables keep their entry and artwork; disable Play and offer Locate EXE or Remove from Library.
- Unsupported executables retained from an older library display an explanation and cannot launch on the current build.

### Add Game

Flow: **Add Game → Browse → Select EXE → Review → Add → Library with the new game focused**.

1. Open at the last browsed folder; fall back to `drive_c`, then the SD root if necessary. Provide shortcuts for Windows drive C: and SD card Z:.
2. List folders first, then `.exe` files, alphabetically. This is enumeration of the folder the user opened, not a library scan. Include Up One Folder, breadcrumbs, and a visible Cancel action.
3. Disable unsupported executable choices with an explanation. Reuse the runtime's `machine_of` callback for validation; an `.exe` extension alone is insufficient.
4. Show a review screen with the executable path, editable title, and icon preview. Default title order: existing per-game title override, PE resource title, filename without `.exe`. Artwork selection is optional and can be completed later.
5. Check normalized executable paths for duplicates. An existing entry produces “Already in your library” and an Open Game Details action, rather than a second card.
6. Commit the entry, return to All Games with it selected, and show a success toast only after the save succeeds. A failed write keeps the review screen open with Retry and Cancel.
7. Cancel leaves the library unchanged. Validate the executable again when adding and immediately before launch in case the file changed or disappeared.

The same browser can serve Locate EXE and artwork selection through explicit modes; selecting a file never implicitly launches it. For the first release, artwork selection accepts PNG, using the existing libpng dependency.

### Input contract

| Input | Behavior |
| --- | --- |
| D-pad / left stick | Move focus among header, shelves, grid, and actions |
| A | Activate focused control; on a game card, open Details |
| B | Return to previous screen; in browser, go up one folder, then return at root |
| X | Open Add Game from Home or Library |
| Y | Open the selected game's options from Home or Library |
| L / R | Switch Home and Library on those screens |
| Minus | Search in Library |
| Plus | Open the existing quit confirmation |
| Touch | Tap activates the same action as A; drag scrolls the shelf or grid |

Use visible Cancel in the browser to leave immediately from a deep folder. A drag must not activate a card on release. L/R tab switching replaces the old L/R paging behavior. Honor Nintendo's button positions through the current `ui_button` mapping.

## Current implementation and changes needed

| Existing area | Present behavior | Planned change |
| --- | --- | --- |
| `launcher.c`: `load_library`, `scan_dir` | Scans `drive_c`, then merges saved paths | Load only explicit catalog entries; remove automatic discovery |
| `launcher.c`: `save_library` | Writes only `added` entries and silently ignores write errors | Use shared catalog persistence with surfaced errors |
| `launcher.c`: `file_browser`, `program_menu` | Browser opens program options and can launch immediately | Return a selected path to Add Game or Locate EXE |
| `launcher.c`: `run_library`, `draw_library`, `draw_card` | Single paged square-icon grid | Screen router, Home shelves, square Library, Details |
| `launcher_ui.c/.h` | Input, drawing, text cache, lists, dialogs | Use one fixed visual system; add shell, image cards, focus and scroll behavior |
| `launcher_console.c`: `launcher_scan` | Independently scans `drive_c` | Read the same catalog and offer a simple Add Game picker |
| `launcher_list.h` | Paths, executable checks, grid navigation helpers | Keep path/argument helpers; add or replace navigation helpers as needed |
| `runtime.c` | Receives selected target after launcher teardown | Preserve launch handoff and current settings resolution |

Suggested new modules, extracted as their responsibilities are implemented:

- `launcher_catalog.c/.h`: membership, metadata, migration, availability, normalization, and persistence; no SDL dependency.
- `launcher_browser.c/.h`: folder listing, path selection and validation; reusable by graphical and console views.
- `launcher_artwork.c/.h`: bounded asynchronous image decoding and cache management, evolved from the existing icon worker.
- `launcher_views.c/.h`: Home, Library, and Details layout/rendering, with explicit state for selection, scroll, and focus region.

Keep `launcher.c` as orchestration, launch actions, and the screen router. Update CMake and the host build script whenever adding a compiled source file.

## Catalog and migration

Introduce `sdmc:/switch/wine/launcher-library-v2.ini` as the single authoritative library catalog. Use a small bounded, versioned INI-style reader with one section per stable game ID. The existing 8 KiB `launcher_kv` buffer is unsuitable for a catalog of up to 256 entries; retain it for small settings files.

Per-entry fields:

- Stable ID, independent of list position and executable path.
- Executable SD path; derive the DOS path through the existing helper.
- Cached display title, creation order, favorite state, and last launch-request order.
- Optional paths to separate landscape and square artwork.

Use monotonically increasing sequence numbers for ordering so the UI does not require an accurate clock. Availability and machine compatibility are derived states, not membership criteria. The per-game `.wine-nx.txt` title remains the user override; the cached catalog title supplies a name when the executable is missing. Keep arguments and runtime options in their current sidecar files to avoid changing runtime behavior.

Persistence rules:

- Stream and bound parsing, enforce existing entry/path limits, reject overlong or malformed records without truncated paths, and handle duplicate entries explicitly.
- Encode/decode reserved characters in values, reject embedded newlines in selected paths, and never silently truncate user data.
- Write to a temporary file in the same directory, check write/flush/close results, and replace the catalog through a platform-tested rename strategy. Keep a recoverable previous copy. Test recovery on Switch SD storage rather than assuming power-loss durability.
- Publish an add/remove/relocate to the UI only after persistence succeeds. Failed writes leave the previous library usable.
- Do not silently replace a corrupt or unsupported-version catalog with an empty library. Offer recovery from the previous copy and preserve the unreadable file.
- Keep the 256-entry limit initially and explain when reached. Reuse removed slots so repeated add/remove operations do not exhaust capacity.

Migration rules:

1. If a valid v2 catalog exists, load it exclusively.
2. Otherwise, import only paths explicitly recorded in `launcher-library.txt`. Preserve missing entries with a filename fallback, deduplicate paths, and load their existing settings when available.
3. Save v2 successfully before considering migration complete; leave the legacy file untouched as a backup. An empty v2 file is a valid catalog and must not trigger re-import.
4. Do not import directory contents, `target.txt`, or games inferred from sidecars. Previously auto-discovered programs must be added by the user.
5. `target.txt` can restore selection only when it matches a catalog member. Existing explicit command-line launch support remains available without modifying catalog membership.

Normalize SD paths consistently for duplicate detection, including separators, redundant components, drive aliases from legacy data, and the current case-insensitive comparison convention. Preserve a usable display/on-disk path; reject paths that escape the allowed SD root or exceed conversion limits.

Locate EXE keeps the stable ID, artwork, favorite state, and ordering metadata, and rejects collisions with another entry. Runtime sidecars resolve beside the newly chosen executable; do not automatically overwrite or copy sidecars from the old location. Explain this in the relocation review.

## Artwork and rendering

- Support separate landscape and square PNGs because cropping a banner into a square can remove the title or subject.
- Fallback order: requested artwork variant → crop of the other variant → contained PE icon on a designed background → title monogram. Never stretch artwork or scale a small executable icon to fill an entire banner.
- Store artwork references in the catalog; handle moved or unreadable artwork with a fallback, without blocking launch.
- Decode off the UI thread and upload/destroy SDL textures on the render thread. Prioritize visible cards, then a small neighboring range.
- Replace the fixed icon-count budget with a byte budget for decoded buffers and textures. Start with a provisional 24 MiB combined artwork budget, use display-sized variants, and measure actual Switch memory consumption before finalizing it.
- Bound input dimensions, file size, and peak decode memory; the cache budget alone does not bound a PNG decoder's temporary allocation.
- Key asynchronous work by stable game ID, artwork variant, and generation. Discard stale results after removal, relocation, or artwork changes.
- Precompute static backgrounds and avoid live blur or shader effects in the first implementation. Use a dimmed selected-game image if affordable.
- Before handing control to Wine, stop/join workers, release decoded buffers, destroy textures, and complete the existing SDL/EGL teardown.

## Implementation sequence

### 1. Establish the visual shell and navigation

Create the fixed visual system, header/footer, Home/Library screen state, both card shapes, and empty-state layouts. Use deterministic in-memory host fixtures until catalog persistence lands in step 2. Keep visual dimensions centralized. Provide host screenshots for Home, Library, Details, and Add Game review.

Exit condition: all proposed layouts fit 1280×720, and controller/touch focus reaches every action without clipping or losing selection.

### 2. Make library membership explicit

Implement the catalog, legacy import, safe writes, duplicate handling, missing entries, and shared folder enumeration. Remove GUI and console startup scanning together. Update the existing host fixtures, which currently rely on discovery.

Exit condition: dozens of unrelated executables can exist under `drive_c` while an empty catalog still produces an empty library. Previously explicit entries survive migration and restart.

### 3. Complete Add Game and management

Build browser selection, review, save, cancellation, duplicate feedback, removal, and Locate EXE. Add the equivalent basic selection flow to the console fallback. Preserve existing per-game settings access.

Exit condition: add → restart → details → play works; adding never launches, cancellation never adds, removal never deletes game files, and write failures never claim success.

### 4. Connect Home, Library, and Details to real data

Implement recent-launch and recently-added shelves, square library scrolling, favorites, search/sort, and restored focus by stable ID. Revalidate before launch and record a launch request only after that validation. Failure to save optional launch history should warn without preventing a valid launch.

Exit condition: all screens show the same catalog membership; only their ordering, filtering, and card shape differ. No actual playtime or successful-session claims are inferred from launch requests.

### 5. Add artwork and finish platform integration

Implement PNG selection and the bounded artwork worker/cache. Honor reduced motion and migrate valid grid-density preferences where they fit the square layout. Remove legacy theme configuration keys.

Exit condition: artwork loads without blocking navigation, invalid images fall back cleanly, and launching releases all launcher graphics resources before Wine starts.

### 6. Validate and document

Extend the existing host harness and sanitizer run, capture the final screenshots, build supported Switch runtime variants, perform device smoke tests, and update `wine-nx-probe/README.md` with the explicit Add Game workflow and migration behavior.

Exit condition: behavioral and device checks below pass. Record device-dependent results separately from host results.

## Validation and acceptance

Behavioral tests should cover the changed contracts rather than implementation details:

- No recursive discovery in GUI or console startup; only registered entries appear on Home and Library.
- Legacy import, empty-v2 handling, missing executables, unsupported versions, duplicate paths, corrupt/truncated catalogs, capacity limits, and failed saves/recovery.
- Add/cancel/re-add/remove/restart/relocate flows, including spaces, non-ASCII names, case differences, long paths, unsupported PE types, and files disappearing after selection.
- Browser root boundaries, deep folders, inaccessible folders, and directories exceeding the current 1024-item limit. Implement pagination or an explicit limit message; never silently imply a partial list is complete.
- Focus after filtering, removal, or sorting; incomplete grid rows; horizontal shelves; return from Details; touch drag versus tap; empty search results.
- PNG failures, image size limits, cache eviction, stale worker results, and worker shutdown during pending decodes.
- Existing launch arguments, per-game settings, C:/Z: translation, target selection, and teardown remain correct.

Use `tests/check-launcher-host.sh` and `tests/launcher_host.c` for scripted flows and screenshots with ASan/UBSan; add focused catalog/browser tests using synthetic fixtures instead of depending only on a fully built SD-card tree. Existing list/settings/PE tests remain part of regression validation where relevant.

On a Switch, check handheld readability, docked scaling, controller mappings, touch scrolling, software keyboard entry/cancel, SD write failures, and actual game launch. Exercise the console fallback before SDL takes the screen; preserve the existing restriction against reinitializing the console after EGL has acquired it.

Measure startup and frame time with empty, 50-entry, and 256-entry catalogs. Target smooth 60 Hz navigation on the accelerated renderer, approximately 16.7 ms per frame, and responsive input during directory reads and artwork loading. Treat this as an implementation target to verify on hardware, not a guaranteed result.

The first release covers the local Wine-NX library and visual redesign. Online stores, accounts, downloads, automatic artwork services, custom collections, and accurate playtime tracking can be separate follow-up work.
