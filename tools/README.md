# Publishing signed updates

The stable channel is `updates/stable.kfu` on the main branch. Every release has its own `updates/<version>/` directory. Maintain a permanent copy of its release notes and signed metadata there.

1. Build and verify a new offline distribution and native updater. Give it a strictly newer semantic version. Write one printable ASCII release note per line in `updates/<version>/notes.txt` (up to eight lines, 112 characters per line).
2. On the original Windows publisher account, run the command below from the repository root. Python needs cryptography and pywin32. The signer compares the SD file list to the 1.1.0 baseline, checks the candidate file hashes and matches the Windows-protected key to the public key in the source. A release that changes other SD components must first gain client support or ship via the full installer. Published versions are immutable.
3. Verify an upgrade from the previous release on a disposable card snapshot, including restart and recovery. Commit the signed channel files and notes together. Create a GitHub Release using the same version tag. Upload the tested standalone NRO, Windows distribution, optional offline update and SHA256SUMS.

```powershell
python tools/publish_feed.py --public-header updater/publisher.h --archive "KEFIRIZ-786-1.3.0.zip" --version 1.3.0 --notes updates/1.3.0/notes.txt --output updates --offline-zip "KEFIRJS-Offline-Update-1.3.0.zip"
```

The publisher key is protected by Windows DPAPI for the account that created this channel and is kept in `%LOCALAPPDATA%/KEFIRJS/publishing/`. Only the public key belongs in version control. Keep a recoverable backup of the Windows publisher profile and its protected key. A different Windows account needs an explicit key migration or a new trusted client release.

## Independent Atmosphere channel

Use the full Atmosphere edition Windows ZIP and pass `--atmosphere-edition --output updates/atmosphere` when signing. The independent client reads its own feed and checks the official Atmosphere core before committing application updates. Its baseline is `base-atmosphere-2.0.0.json`. System runtime and native layout migrations use the full Windows installer.
