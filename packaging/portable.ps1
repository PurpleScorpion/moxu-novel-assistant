param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$Destination
)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $Destination) { throw 'Choose a new staging directory.' }
$stage = (New-Item -ItemType Directory -Path $Destination).FullName
Copy-Item -LiteralPath $Executable -Destination (Join-Path $stage 'Moxu.exe')
Copy-Item -LiteralPath (Join-Path (Split-Path -Parent (Resolve-Path $Executable).Path) 'yaml-cpp-LICENSE.txt') -Destination $stage
Set-Content -LiteralPath (Join-Path $stage 'qt.conf') -Value "[Paths]`nPrefix=.`nPlugins=." -Encoding ascii
& windeployqt --release --compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler --exclude-plugins qsqlmimer,qsqlodbc,qsqlpsql (Join-Path $stage 'Moxu.exe')
if ($LASTEXITCODE) { throw 'Qt deployment failed.' }
$originalPath = $env:Path
try {
    $env:Path = "$env:SystemRoot\System32;$env:SystemRoot"
    # A GUI executable needs an explicit wait before its result can be checked.
    $probe = Start-Process -FilePath (Join-Path $stage 'Moxu.exe') -ArgumentList '--startup-check' -WorkingDirectory $stage -WindowStyle Hidden -PassThru -Wait
    if ($probe.ExitCode -ne 0) { throw "Packaged startup or SQLite check failed ($($probe.ExitCode))." }
} finally {
    $env:Path = $originalPath
}
Compress-Archive -LiteralPath $stage -DestinationPath "$stage.zip"
