# CastWeave

**One plugin. Two docks. Less dashboard.**

CastWeave brings Twitch chat and channel controls into OBS without turning a simple streaming
workflow into a control-room puzzle.

<p align="center">
  <img src="https://count.getloli.com/@marek-codex.castweave?theme=booru-lewd" alt="CastWeave visitor counter">
</p>

## What works today

- Separate **CastWeave Chat** and **CastWeave Streams** docks.
- Live Twitch chat with display names, colors, badges, static emotes, deletion events and reconnects.
- Confirmed Twitch message sending; rejected messages remain in the input.
- Twitch title, category, tags and classification editing with save verification.
- Secure remembered login through Windows Credential Manager.
- A shared settings window from the Chat gear or **Tools > CastWeave Settings**.
- Verified GitHub updates with download checks, rollback and normal OBS restart handling.

YouTube and Kick currently provide visual destination placeholders and sample-chat filtering. Their
login, live chat and streaming outputs are still in development. Destination toggles save the user's
selection but do not start a stream.

## Install

1. Close OBS Studio.
2. Download and run the Windows Setup installer from
   [Releases](https://github.com/zerithvt-Coder/castweave/releases).
3. Reopen OBS and enable **CastWeave Chat** and **CastWeave Streams** from the **Docks** menu.

The portable ZIP can be extracted into `C:\ProgramData\obs-studio\plugins`. When upgrading from
StreamDock, move its old `streamdock` folder outside the OBS plugin directory so both versions cannot
load together. Existing CastWeave settings and remembered credentials are kept during upgrades.

## Twitch connection

Open **CastWeave Streams**, expand Twitch and choose **Connect Twitch**. Approve the device login in
your browser. CastWeave requests only the permissions used for channel editing and chat.

Older saved accounts may need one additional **Enable Twitch chat** approval. Once connected, new
messages appear in CastWeave Chat and the message box sends as the connected account. Messages sent
before connection are not backfilled.

## Updates and diagnostics

Open the Chat gear or **Tools > CastWeave Settings** to check for updates, change the chat-history
limit or open the diagnostic log. Updates are accepted only from this repository and must match the
release filename, declared size and SHA-256 digest before installation.

Diagnostics are stored as JSON lines under `%LOCALAPPDATA%\CastWeave\logs` and rotate at 1 MiB.
They record bounded connection and update results without tokens, authorization headers, device
codes, OAuth bodies or chat-message text.

## Build

Requirements: Windows x64, Visual Studio 2022 with Desktop C++, Windows SDK 10.0.22621 or newer,
and CMake 3.30 or newer.

```powershell
cmake --preset windows-x64 -DSTREAMDOCK_BUILD_PREVIEW=ON
cmake --build --preset windows-x64
cmake --install build_x64 --config RelWithDebInfo --prefix stage
./scripts/test-update.ps1
./scripts/build-installer.ps1 -StagePath stage -OutputDirectory artifacts
```

OBS 31.1.1 and the pinned Qt dependencies are downloaded from the official OBS build dependencies
and verified against hashes in `buildspec.json`. The intended runtime is OBS 32.2.2 on Windows x64.

## Project status

CastWeave is beta software. Offline checks cover Twitch chat parsing, permissions, message delivery
results, reconnects, emote rendering, editor behavior, update validation, rollback, cancellation and
restart handoff. Real account permissions and platform responses still require user testing.

## License

CastWeave is based on `obsproject/obs-plugintemplate` and is distributed under GPL-2.0-or-later.
Platform marks identify their respective services; CastWeave is not affiliated with Twitch,
YouTube, Kick or Meld.
