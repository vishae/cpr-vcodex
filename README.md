> **This is a personal fork of [franssjz/cpr-vcodex](https://github.com/franssjz/cpr-vcodex)**, based on release [`1.3.0.35-cpr-vcodex`](https://github.com/franssjz/cpr-vcodex/releases/tag/1.3.0.35-cpr-vcodex). For the full feature set, install instructions, dictionary/font setup, and everything else CPR-vCodex does, see franssjz's repo and his [README](https://github.com/franssjz/cpr-vcodex#readme) -- this fork doesn't change any of that. Below is only what's different here.

# cpr-vcodex (personal fork)

<p align="center">
  <img src="./docs/images/500x100.png" alt="CPR-vCodex logo" width="500" />
  <br />
  <sub>Logo contributed by Which-Estimate4566, from the upstream project.</sub>
</p>

## What I changed in this fork

Two changes on top of franssjz's `1.3.0.35-cpr-vcodex`, both around KOReader sync:

- **Fixed KOReader sync sending the wrong date.** The outgoing progress update never included a timestamp field, even though the rest of the sync protocol already supported one. Syncs now send the actual current time. (In practice this turned out to be a no-op against the reference sync server, which stamps its own received-date regardless -- see the commit message for the full story. Kept anyway since it completes a field the protocol's own download path already expects.)
- **Added support for multiple KoReader sync accounts.** Previously the firmware could only remember one KoReader server/username/password at a time. This fork adds a profile list (`Settings > KOReader Sync > KOReader Profiles`) for saving several accounts and switching which one is active, the same way OPDS servers already worked. Existing single-account setups keep working unchanged.

  Why this matters in practice:
  - If the default `https://sync.koreader.rocks` goes down -- which it can -- a saved backup server profile can be switched to without losing or overwriting the original credentials.
  - For a self-hosted [koreader-sync-server](https://github.com/koreader/koreader-sync-server), the same server may need to be reached differently depending on which network the device is on (e.g. a LAN address at home versus a different address elsewhere). Separate profiles let each network's correct address be saved and switched to, rather than re-entering the server URL every time.

  How it's stored: the full profile list plus which one is active lives in a new `/.crosspoint/koreader_profiles.json`. The original `/.crosspoint/koreader.json` (the file stock crosspoint-reader/CrossPoint Reader also reads) keeps its old single-account shape and is always rewritten to mirror whichever profile is active, so switching back to stock firmware on the same SD card still works.

  **Known limitation:** that mirroring only runs one way. If you edit KoReader credentials from a different firmware that only knows the single-account `koreader.json` shape (e.g. stock CrossPoint Reader), this fork's profile store won't notice -- you'd need to re-enter that change here. No profiles are ever lost, they just won't auto-update from an edit made elsewhere.

Full detail in the [commit history](https://github.com/vishae/cpr-vcodex/commits/master).

## Flashing

Grab the latest `*-cpr-vcodex.bin` from this fork's [releases](https://github.com/vishae/cpr-vcodex/releases) (or build one locally with `pio run -e default`), then flash it the same way franssjz's own [Easy installation](https://github.com/franssjz/cpr-vcodex#easy-installation) instructions describe.

## Credits

- the **CrossPoint Reader** project and **franssjz/cpr-vcodex** for everything this fork builds on
- [zgredex](https://github.com/zgredex) for the original `Lyra Carousel` Home theme, adapted to CPR-vCodex by [erickosanchezj](https://github.com/erickosanchezj)
- Which-Estimate4566 for the logo artwork

---

Not affiliated with Xteink or any manufacturer of the X4 hardware.
