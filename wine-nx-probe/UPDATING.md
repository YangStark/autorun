# Launcher updates

Settings > System > Check for update reads the latest stable release from
`danfromtico/autorun`. A background check at boot only shows a notification;
installation always needs confirmation. Development builds newer than the
published release are not downgraded.

The launcher verifies the release asset's SHA-256 digest, then extracts the full
`switch/wine` archive. Existing configuration, game files, saves, artwork and
downloaded graphics versions are kept when paths overlap. Packaged runtime DLLs,
fonts, NLS data, bundled graphics DLLs, licenses and the main NRO are replaced.
New files and directories from the release are installed too.

Allow free SD space for the download, extracted files and backups. Installation
can be cancelled before files are replaced. Keep the console powered on during
replacement and restart. AMD64 builds reject packages missing the AMD64 runtime.

An interrupted installation is rolled back before Wine loads DLLs. Backups stay
under `switch/wine/updates` until the updated launcher opens successfully. If a
power interruption prevents the main NRO from starting, open
`switch/wine/updates/previous.nro` from the Homebrew Menu to restore the previous
installation. Do not remove the update directory while recovery is pending.

The Switch build requires devkitPro's `switch-minizip`, alongside the existing
curl and zlib dependencies. Release identification is embedded from Git at build
time. Builds without Git metadata can show releases but do not guess whether a
release is newer.
