# eeLinux build entry point for the current Windows workspace.
param(
    [ValidateSet('help','plan','check','kernel-source','kernel-port','kernel-modernize','kernel-audit','toolchain','ps2dev','firmware','userland','initramfs','kernel','image','all','reference-system','emulator-userland-build','emulator-smoke','emulator-prepare','xorg-apps-update','xorg-apps','native-xorg','usb-image-2gb','dvd-kernel-pal','dvd-iso-slim-pal','dvd-iso-fat-pal','dvd-isos-pal','pal-media')]
    [string]$Target = 'help',
    [string]$Distribution = 'Debian',
    [ValidateSet('base','desktop','full')][string]$Profile = 'desktop',
    [int]$Jobs = 2,
    [string[]]$MakeVariables = @()
)
$ErrorActionPreference = 'Stop'
if ($Jobs -lt 1) { throw 'Jobs must be positive' }
foreach ($Setting in $MakeVariables) {
    if ($Setting -notmatch '^[A-Za-z][A-Za-z0-9_]*=.*$') { throw 'Expected NAME=value make variables' }
}
# Build output defaults to the native Linux filesystem through Makefile WORK.
& wsl.exe --distribution $Distribution --cd $PSScriptRoot --exec make $Target "PROFILE=$Profile" "JOBS=$Jobs" 'EMULATOR_HOST=windows' @MakeVariables
exit $LASTEXITCODE
