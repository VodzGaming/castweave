# CastWeave

Native OBS docks for multichat and multistream controls.

**0.5.0 adds live Twitch chat, sending, and automatic reconnection using your remembered Twitch account.**

## Available
- Separate CastWeave Chat and CastWeave Streams docks.
- Twitch, YouTube and Kick platform marks; sample moderator, subscriber and VIP badges.
- Capped sample chat history, platform filters and highlighted sample events.
- Expandable destination rows with saved selections and per-platform drafts.
- Built-in GitHub updates from VodzGaming/castweave; no repository entry required.
- Optional startup check, verified update download and Restart OBS button.

## Still in development
YouTube/Kick login and chat, third-party emotes, animated emotes, and streaming outputs.
Sample role badges are illustrative. Toggles do not start streams; drafts are not sent to platforms.

## Install
Close OBS. Extract the ZIP's castweave folder into C:/ProgramData/obs-studio/plugins.
When upgrading from StreamDock, move its old streamdock folder outside the OBS plugins directory
to avoid loading both. Old settings are retained for migration.
Reopen OBS and enable Docks > CastWeave Chat and Docks > CastWeave Streams.

## Build
Windows x64: Visual Studio 2022 Desktop C++, Windows SDK 10.0.22621, CMake 3.30+.

    cmake --preset windows-x64
    cmake --build --preset windows-x64
    cmake --install build_x64 --config RelWithDebInfo --prefix stage

Visual Studio 2026:

    cmake -S . -B build_local -G "Visual Studio 18 2026" -A x64
    cmake --build build_local --config RelWithDebInfo

OBS SDK 31.1.1 and hash-verified Qt dependencies come from the OBS template.
Intended runtime: OBS 32.2.2. This renamed version requires a runtime check.
No measured memory or CPU claims yet.

## Releases
GitHub Actions builds Windows packages on main, pull requests and manual runs.
A v-prefixed tag matching buildspec.json produces a draft release after a successful build.
Publish the reviewed draft to make it available to the built-in release checker (including preview releases).
Updates download and verify the Windows ZIP. Restart OBS prepares the installer, closes OBS normally and reopens it after installation. No PC restart is needed.

## Attribution
Based on obsproject/obs-plugintemplate, under GPL-2.0-or-later; see LICENSE.
Platform marks identify the corresponding services. CastWeave is not affiliated with Twitch,
YouTube, Kick or Meld. Sample role artwork is not channel-specific Twitch badge artwork.

## Twitch channel manager (0.3.0)
Expand Twitch in CastWeave Streams and click Connect Twitch. Approve the device login in your
browser. CastWeave requests channel:manage:broadcast, chat:read and user:write:chat, validates the login, and loads your
current title, category and tags. Remember this Twitch account stores tokens in Windows Credential Manager for the current Windows user.
Tokens are validated on startup and hourly, and renewed before expiry. Disconnect deletes the saved login.
Unchecking Remember deletes the stored login while keeping the current connection.

Type at least one category character for live suggestions (350 ms delay); select a result to
bind the real category ID. Save sends changed fields only when clicked and reads them back from Twitch to verify the result.
Search results from older queries are ignored. Logging out invalidates outstanding callbacks.
Network timeouts and expired credentials leave no false success state.
Live Twitch badges use Twitch artwork; Sample chat uses illustrative badges.

Verification: Windows compilation, disconnected-state/invalid-input/no-change smoke checks,
and Twitch acceptance of the registered Client ID for device authorization. Actual user-approved
login, live search results and channel writes still require an end-to-end OBS test.


## 0.3.2 stream editor
Tags are removable chips: press Enter after each tag or click Save to include the pending tag.
Save is the single channel update action. Category search now starts at one character.
Classification selection includes the six editable Twitch labels, preserving other server labels.
The default browser/device authentication remains unchanged.


## 0.4.0 updates and diagnostics
Download update shows progress and validates the GitHub asset size and SHA-256 digest.
Restart OBS starts the bundled Windows helper. OBS closes after preparation succeeds and
reopens automatically when installation finishes. The helper validates archive paths, backs up the old
files and restores the backup if copying fails. Installation failures are recorded in the log.
The supported installation layout is castweave/bin/64bit/castweave.dll with the bundled data folder.

Open diagnostic log is available below Save and in Settings.
Logs are JSON lines at %LOCALAPPDATA%/CastWeave/logs/castweave.log, rotated at 1 MiB.
They include HTTP result codes, tags before/after Save, verified channel tags and update results.
They exclude tokens, authorization headers, device codes and OAuth response bodies.
To test removing a tag: remove its chip, click Save, and wait for "Saved and verified on Twitch".
A save_verified record contains the tags returned by Twitch; merely removing a chip does not save it.
Category results show cover art when Twitch supplies it; no invented viewer/follower counts.

Local verification: preview/offline editor checks; real release download with valid and invalid
checksums; temporary-directory installer tests for archive validation, copying and backup.
Live account save/read-back and installation into running OBS require the user's next runtime test.


## 0.4.1 remembered login and category details
Connect once with Remember enabled after upgrading from 0.4.0. A saved account reconnects
automatically on the next OBS start. Revocation or an inactive refresh token expiring still
requires fresh authorization. Twitch device refresh tokens rotate after use; the replacement
is stored immediately. Credentials never enter QSettings or diagnostic logs.
Only the currently connected Twitch account is remembered; multiple saved accounts are not implemented.
Category viewer counts are estimates from up to five pages of 100 live streams, deduplicated by
stream ID, when loading/selecting a category. Partial counts are labelled. This is not Twitch's
official category total. Category follower totals and genre labels are unavailable in the public API.
Keep current category restores the channel's saved category without sending changes.
Preview tests use isolated test credentials and preview.log, never the user's Twitch credential.
Validation includes real Windows credential write/read/delete and mocked expired-token renewal,
temporary outage retention, revoked-login removal, paginated viewer deduplication and category reset.
The user's next OBS restart is the remaining end-to-end test with a real Twitch account.


## 0.4.2 restart flow
The verified download shows Update ready and Restart OBS. The plugin checks OBS output activity
before preparing the helper and again before closing the main window. Streaming, recording,
replay buffer and virtual camera must be stopped. OBS's normal close handling is used; cancelling
it writes a cancellation marker, preserving the downloaded update.
The helper extracts and validates before signalling readiness, waits for OBS to exit, installs
with backup/rollback, and relaunches the same executable in its executable directory.
Portable mode is retained; automatic start-streaming/recording flags are not replayed.
Other OBS instances must close first. A named mutex prevents concurrent installation.
Per-attempt status and updates/last-update.json provide visible failure/success reporting.
Preview checks cover blocked/cancelled restart callbacks; temporary-fixture tests cover helper
readiness, installation-before-relaunch, portable relaunch arguments and cancellation.
Actual OBS shutdown/relaunch remains a user runtime check.


## 0.4.3 category result details
The dropdown now renders the cover, category name and viewer estimate inside each result row.
A separate data role stores the estimate, so selecting a row fills only the category name.
Counts load for six visible rows after a 700 ms pause, with at most two scans for the current
query at a time and up to five pages per scan. A bounded 40-category cache lasts two minutes.
Older query callbacks cannot overwrite new rows. Scrolling queues the newly visible rows.
Offline checks cover paginated deduplication, cache reuse, stale-count rejection and clean names.
The category-row preview uses sample numbers and an artwork placeholder; live data needs a user test.
Follower totals and game genres remain unavailable from the public Twitch category API.


## 0.4.4 faster viewer estimates
Removed the additional 700 ms delay. Up to six visible category scans run concurrently for
the current query. Each first-page estimate is painted immediately and refined by subsequent
pages. Cached estimates still appear with the category name; uncached estimates require
a separate network response. Existing page limits, deduplication and stale-query guards remain.
Offline checks verify the first-page count is visible before the second response is processed.


## 0.4.5 interrupted restarts
During restart, the installer tracks the requesting OBS process ID. A different OBS process
causes a visible failure instead of extending the wait. Exit checks use a 250 ms interval.
No processes are force-terminated. Slow WebSocket/plugin shutdown remains outside this updater.
Installer fixtures cover early reopening and use a five-second timeout to bound failing tests.


## 0.5.0 live Twitch chat
The Chat dock receives new messages from the connected account's own channel, with display names,
username colors, global/channel badges and static Twitch emotes. Existing chat history is not fetched.
Enter or Send to Twitch submits the message as your account; Twitch must confirm delivery before
its input is cleared. Rejected messages stay in the input. A network timeout has uncertain delivery:
check Twitch before retrying. No automatic resend occurs. Third-party/animated emotes are not included.

Existing accounts need a one-time Enable Twitch chat approval because older tokens only had stream
editing permission. Remember this Twitch account continues to use Windows Credential Manager.
Chat reconnects on OBS startup and after token renewal or connection loss (bounded 1–30 second
backoff). Disconnect stops retries and clears live account messages. The destination output toggle
is independent of chat. Chat works while the stream is offline.

The plugin uses Qt TLS IRC to receive and the Helix Send Chat Message API to confirm sends.
No Node/Python helpers or blocking socket shutdown are used. Twitch deletion, timeout and chat-clear
events remove affected messages. Duplicate message IDs are ignored. Artwork is fetched from Twitch's
CDN with request/size limits and a bounded cache. Diagnostics record connection and send results,
plus incoming message IDs, without chat text, tokens or authorization headers.

Offline tests cover fragmented IRC input, duplicate/wrong-channel messages, deletions, send rejection
and success, late callbacks after logout, reconnect/authentication failure, refreshed permissions,
and Unicode emote offsets/HTML escaping. Real sending/receiving with newly approved chat permissions
still needs an OBS account test; no test message is sent to a real channel by the build checks.
