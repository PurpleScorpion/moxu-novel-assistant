param(
    [Parameter(Mandatory)][string]$Executable,
    [string]$Destination,
    [switch]$Publish
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$project = [IO.File]::ReadAllText((Join-Path $repo 'CMakeLists.txt'))
$versionPattern = '(?im)^\s*project\(\s*Moxu\s+VERSION\s+(\d+(?:\.\d+){1,3})(?=\s|\))'
$version = [regex]::Match($project, $versionPattern).Groups[1].Value
if (!$version) { throw 'Cannot read the Moxu project version from CMakeLists.txt.' }
$exePath = (Get-Item -LiteralPath $Executable).FullName
$exeDir = Split-Path -Parent $exePath
foreach ($dir in @($exeDir, (Split-Path -Parent $exeDir))) {
    $cache = Join-Path $dir 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache) {
        $builtVersion = [regex]::Match([IO.File]::ReadAllText($cache), '(?m)^CMAKE_PROJECT_VERSION:STATIC=([^\r\n]+)').Groups[1].Value
        if ($builtVersion -ne $version) { throw "Build version '$builtVersion' differs from $version. Rebuild before packaging." }
        break
    }
}
if (!$Destination) { $Destination = Join-Path $repo "dist/Moxu-$version-win64" }
if ((Test-Path -LiteralPath $Destination) -or (Test-Path -LiteralPath "$Destination.zip")) {
    throw 'Choose a new staging directory and ZIP path with -Destination.'
}
$downloadDir = Join-Path $repo 'downloads'
$download = Join-Path $downloadDir 'Moxu-win64.zip'
$readme = Join-Path $repo 'README.md'
$marker = [regex]'(?<=<!-- moxu-version -->)\d+(?:\.\d+){1,3}(?=<!-- /moxu-version -->)'

function Get-ReleaseReadme {
    $content = [IO.File]::ReadAllText($readme)
    if ($marker.Matches($content).Count -ne 2) { throw 'Expected two marked release versions in README.md.' }
    return $marker.Replace($content, $version)
}

$null = Get-ReleaseReadme
if ($Publish) {
    $committedProject = git -C $repo show HEAD:CMakeLists.txt
    if ($LASTEXITCODE -ne 0 -or [regex]::Match(($committedProject -join "`n"), $versionPattern).Groups[1].Value -ne $version) {
        throw 'Commit the project version before running -Publish.'
    }
}
$stage = (New-Item -ItemType Directory -Path $Destination).FullName
Copy-Item -LiteralPath $exePath -Destination (Join-Path $stage 'Moxu.exe')
Copy-Item -LiteralPath (Join-Path $exeDir 'yaml-cpp-LICENSE.txt') -Destination $stage
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

# Serialize promotion and its Git commit. Extraction/build directories can be on another volume.
$mutex = [Threading.Mutex]::new($false, 'Local\MoxuPortableRelease')
try {
    try { $null = $mutex.WaitOne() } catch [Threading.AbandonedMutexException] { }
    $newReadme = Get-ReleaseReadme
    [IO.Directory]::CreateDirectory($downloadDir) | Out-Null
    $id = [Guid]::NewGuid().ToString('N')
    $zipTemp = Join-Path $downloadDir ".$id.zip"
    $previousZip = Join-Path $downloadDir ".$id.bak"
    $readmeTemp = Join-Path $repo ".$id.md"
    $hadDownload = [IO.File]::Exists($download)
    try {
        [IO.File]::Copy("$stage.zip", $zipTemp)
        $updateReadme = $newReadme -cne [IO.File]::ReadAllText($readme)
        if ($updateReadme) { [IO.File]::WriteAllText($readmeTemp, $newReadme, [Text.UTF8Encoding]::new($false)) }
        if ($hadDownload) { [IO.File]::Replace($zipTemp, $download, $previousZip) }
        else { [IO.File]::Move($zipTemp, $download) }
        try {
            if ($updateReadme) { [IO.File]::Replace($readmeTemp, $readme, "$readmeTemp.bak") }
        } catch {
            if ($hadDownload) { [IO.File]::Replace($previousZip, $download, $zipTemp) }
            else { [IO.File]::Delete($download) }
            throw
        }
        [IO.File]::Delete($previousZip)
    } finally {
        [IO.File]::Delete($zipTemp)
        [IO.File]::Delete($readmeTemp)
        [IO.File]::Delete("$readmeTemp.bak")
    }
    if ($Publish) {
        $paths = @('README.md', 'downloads/Moxu-win64.zip')
        git -C $repo diff --quiet HEAD -- @paths
        if ($LASTEXITCODE -eq 1) {
            git -C $repo add -- @paths
            if ($LASTEXITCODE -ne 0) { throw 'Could not stage the release files.' }
            git -C $repo commit --only -m "Publish Moxu $version for Windows" -- @paths
            if ($LASTEXITCODE -ne 0) { throw 'Could not commit the release files.' }
        } elseif ($LASTEXITCODE -ne 0) { throw 'Could not inspect the release files.' }
        git -C $repo push origin HEAD:refs/heads/main
        if ($LASTEXITCODE -ne 0) { throw 'Release prepared locally. Resolve the push error and retry: git push origin HEAD:refs/heads/main' }
    }
    Write-Output "Moxu $version packaged: $stage.zip"
    Write-Output "Updated downloads/Moxu-win64.zip and README.md."
} finally {
    $mutex.ReleaseMutex()
    $mutex.Dispose()
}
