# KEFIRIZ native theme renderer

Meridian uses the layout resources from your own Horizon 20.5.0 system. The version marker `ver.cfg` and the `qlaunch/lyt`, `playerselect/lyt` and `mypage/lyt` subfolders belong together. The Windows installer accepts the KEFIRIZ capture or an existing personal export in `themes/systemData/extracted`. Older KEFIRIZ 786 installations can also read a single flat 2.8.3 export.

The renderer checks the 20.5.0 version marker, the HOME layout family and the original screen resources. Missing optional screens are reported. Meridian keeps Horizon's lock screen. The legacy 786 route can prepare a lock screen when its matching NXThemes compatibility component is already active on the same card.

After a successful theme recovery retry, a subsequent artwork pass can add User Page and Player Selection from a complete personal flat export. The installer chooses it only when its firmware marker, all five qlaunch originals and any applets available in both exports agree. A recovery hold or conflicting export keeps this expansion disabled. All screens in a render come from a single export.

The output directory must be an empty PC staging directory. A successful run prints one ready SD-relative path per line. The renderer keeps every screen in memory until all present, eligible screens have passed template matching, compression round-trip, texture dimensions, format, color channels and encoded pixel checks. Exit code 0 means HOME was prepared. Code 2 identifies missing personal HOME resources or a different cache version. Code 1 indicates an unsuccessful operation. The distribution installer handles file deployment, backup and restore.

The lock-support argument is optional.

    Render.exe --base E:\themes\systemData\extracted --palette Light --output C:\Temp\KEFIRIZ-preview --stock-lock yes

## System color and Lock Screen

Meridian Ember places the HOME gallery above three groups of system buttons. A pearl chevron with a coral tip identifies the dock. A folded edge runs along the footer of all six eligible screens. Each applet button moves with its touch target, focus cursor and label. Basic White and Basic Black select the native background, text and button colors.

Settings and News use an accent bar on the selected category or channel. All Software and News use a four-pixel selection border on game, group and article cards. Their backdrops share a header band, vertical rule and footer. An available User Page or Player Selection export also receives the backdrop. The renderer uses nine Meridian button masks and checks their encoded pixels and the original background materials before writing. Run `python config/kefiriz/standalone/tests/test_native_palettes.py` from the project root to test the gallery, dock and native palette with synthetic data. [Native picture properties](https://github.com/KillzXGaming/Switch-Toolbox/wiki/BFLYT-Editing) · [Native material colors](https://layoutdocs.themezer.net/guide/6-layouts/usd-sections/)

Explicitly applying artwork can add a newly available screen. Each change has
a separate rollback receipt. After theme recovery, boot successfully and export
the layout files again on this console. The retry operation checks each eligible
export before installing the new artwork and releasing the recovery hold. The
legacy lock screen requires the
exported Entrance.szs and the matching active compatibility component. The
renderer checks all active Atmosphere patchset directories for that component
by name and SHA-256, including build IDs with trailing zero bytes omitted.
[Atmosphere loader directory convention](https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/components/modules/loader.md)

## Rebuild

The C# sources in SwitchThemesCommon are from the upstream SwitchThemes 4.8.3 tree at commit 3d1f6114e14a89292590e2c87e91b58a9e6bf49d. Render.exe compiles against .NET Framework 4.8. The packaged runtime library is Newtonsoft.Json 13.0.2 from the official Windows release. The GPLv2 and Newtonsoft.Json MIT license texts accompany this source. [SwitchThemes source](https://github.com/exelix11/SwitchThemeInjector/tree/3d1f6114e14a89292590e2c87e91b58a9e6bf49d/SwitchThemesCommon) · [Newtonsoft.Json](https://github.com/JamesNK/Newtonsoft.Json/tree/13.0.2)

With the .NET Framework 4.8 targeting pack installed, run the following command. The runtime directory should contain the matching Newtonsoft.Json.dll.

    python build.py --csc "<path to Visual Studio Roslyn csc.exe>" --runtime "<path to native-render>"

With Python and Pillow 11.3.0 installed, run the command below to generate the DDS artwork. Each theme archive is unpacked with SwitchThemes 4.8.3. Backgrounds use single-level 1280×720 DXT1. The nine toolbar icons use 64×56 DXT5 and the lock image uses 184×168 DXT5.

    python build_assets.py --tool "<path to SwitchThemes.exe>" --themes "<release sd/themes/KEFIRIZ>" --out "<native-render/assets>"

The renderer expects exports made from the user's current system. Screen compatibility relies on the origin of those files as well as the version marker and layout checks. Tests on synthetic graphics cover rendering and failure handling. A physical console is needed to verify the boot-time result.
