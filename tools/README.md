# Publishing signed updates

The native clients read `updates/native/stable.kfu` (786 edition) or `updates/atmosphere/native/stable.kfu` (independent Atmosphere edition) on the main branch. Keep each version's notes, signed metadata and payload under the corresponding channel directory. Earlier clients read the bootstrap feeds in `updates/stable.kfu` and `updates/atmosphere/stable.kfu`.

1. Build and verify a new offline distribution and native updater. Give it a strictly newer semantic version. Write one printable ASCII release note per line in `updates/<version>/notes.txt` (up to eight lines, 112 characters per line).
2. On the original Windows publisher account, run the command below from the repository root. Python needs cryptography and pywin32. The signer compares the SD file list to the 1.1.0 baseline, checks the candidate file hashes and matches the Windows-protected key to the public key in the source. A release that changes other SD components must first gain client support or ship via the full installer. Published versions are immutable.
3. Verify an upgrade from the previous release on a disposable card snapshot, including restart and recovery. Commit the signed channel files and notes together. Create a GitHub Release using the same version tag. Upload the tested standalone NRO, Windows distribution, optional offline update and SHA256SUMS.

```powershell
python tools/publish_feed.py --native --public-header updater/publisher.h --archive "KEFIRIZ-786-1.3.0.zip" --version 1.3.0 --notes updates/native/1.3.0/notes.txt --output updates/native --offline-zip "KEFIRJS-Repair-786-1.3.0.zip"
```

The publisher key is protected by Windows DPAPI for the account that created this channel and is kept in `%LOCALAPPDATA%/KEFIRJS/publishing/`. Only the public key belongs in version control. Keep a recoverable backup of the Windows publisher profile and its protected key. A different Windows account needs an explicit key migration or a new trusted client release.

## Independent Atmosphere channel

Use the full Atmosphere edition Windows ZIP and pass `--atmosphere-edition --native --output updates/atmosphere/native` when signing native updates. The independent client reads its own feed and checks the official Atmosphere core before committing changes. Its baseline is `base-atmosphere-2.0.0.json`. System runtime and native layout migrations use the full Windows installer.

The 2.0.2 and 1.2.2 clients support signed service and artwork updates. Sign with `--native` in `updates/atmosphere/native` or `updates/native`. Earlier clients use the four-application bootstrap before switching channels. Phone repair kits include the two verified services at their live paths, plus the signed complete update.

For a Preview release, pass `--channel preview --native`. Preview metadata carries a signed channel field. Stable clients reject previews placed in the Stable slot. Both channel pointers reference immutable, versioned payloads. A preview can follow the published Stable envelope for the same edition when there is no newer candidate.

Aurora 2.1.1 and later use `--full-preview --output updates/atmosphere/native/full` for Atmosphere Preview. Supply `--offline-zip` on that full release. The original Preview URL is pinned to the two-stage 2.1.1 transition for clients that predate the expanded Profiles allowlist. Generate that bridge once from the same verified Windows archive using `--bridge-profiles-from KEFIRIZ-Aurora-2.1.0-r2.zip --output updates/atmosphere/native` and its own notes. The legacy payload keeps the published 2.1.0 Profiles digest. Reopening the new updater completes the transition from the independently signed full feed.
