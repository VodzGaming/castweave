# CastWeave 0.5.0 - live Twitch chat

- Receive new Twitch messages in CastWeave Chat, with names, colors, real badges and static Twitch emotes.
- Press Enter or Send to Twitch to send as your connected account. Failed sends remain in the input; delivery is checked with Twitch.
- Chat reconnects using your remembered account after OBS starts, token renewal or a connection drop.
- Accurate chat status replaces the misleading sample-only account message. Deleted messages and cleared chat are removed.
- All chat networking runs inside the plugin; no Node or Python helpers.

After installing, click Enable Twitch chat in the Chat dock and approve the additional chat permissions once. Keep Remember this Twitch account checked. Wait for Connected to #yourchannel, then send a fresh test message from Twitch and from CastWeave. Messages sent before connection are not fetched.

Built and checked with offline chat, permission, sending, reconnect and rendering fixtures. Live account testing requires that new approval. Twitch emotes are static; third-party emotes and YouTube/Kick chat are not included yet.

Download update, then Restart OBS. Wait for OBS to reopen automatically; a slow OBS shutdown can still delay installation.
