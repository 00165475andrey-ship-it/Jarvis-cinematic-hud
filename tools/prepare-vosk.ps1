param(
    [string]$Destination = "package"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

if (-not [Environment]::Is64BitOperatingSystem) {
    throw "The current package contains the official Windows x64 Vosk runtime and requires 64-bit Windows."
}

$version = "0.3.45"
$modelName = "vosk-model-small-ru-0.22"
$temp = Join-Path $env:RUNNER_TEMP "jarvis-vosk"
$runtimeZip = Join-Path $temp "vosk-win64-$version.zip"
$modelZip = Join-Path $temp "$modelName.zip"
$runtimeUrl = "https://github.com/alphacep/vosk-api/releases/download/v$version/vosk-win64-$version.zip"
$modelUrl = "https://alphacephei.com/vosk/models/$modelName.zip"

New-Item -ItemType Directory -Force -Path $temp, $Destination | Out-Null
Write-Host "Downloading official Vosk Windows x64 runtime $version..."
Invoke-WebRequest -UseBasicParsing -Uri $runtimeUrl -OutFile $runtimeZip
Write-Host "Downloading Russian offline model $modelName..."
Invoke-WebRequest -UseBasicParsing -Uri $modelUrl -OutFile $modelZip

$runtimeDir = Join-Path $temp "runtime"
$modelDir = Join-Path $temp "model"
if (Test-Path $runtimeDir) { Remove-Item -LiteralPath $runtimeDir -Recurse -Force }
if (Test-Path $modelDir) { Remove-Item -LiteralPath $modelDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $runtimeDir, $modelDir | Out-Null
Expand-Archive -LiteralPath $runtimeZip -DestinationPath $runtimeDir -Force
Expand-Archive -LiteralPath $modelZip -DestinationPath $modelDir -Force

$dlls = Get-ChildItem -LiteralPath $runtimeDir -Filter "*.dll" -File -Recurse
$voskDll = $dlls | Where-Object { $_.Name -ieq "libvosk.dll" } | Select-Object -First 1
if (-not $voskDll) {
    throw "The official Vosk archive did not contain libvosk.dll."
}
foreach ($dll in $dlls) {
    Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $Destination $dll.Name) -Force
}

$foundModel = Get-ChildItem -LiteralPath $modelDir -Directory -Recurse |
    Where-Object { $_.Name -eq $modelName } |
    Select-Object -First 1
if (-not $foundModel) {
    throw "Could not find extracted model folder $modelName."
}

$targetModel = Join-Path $Destination "models\$modelName"
New-Item -ItemType Directory -Force -Path (Split-Path $targetModel -Parent) | Out-Null
if (Test-Path $targetModel) { Remove-Item -LiteralPath $targetModel -Recurse -Force }
Copy-Item -LiteralPath $foundModel.FullName -Destination $targetModel -Recurse -Force

# Validate the actual model files used by the Kaldi/Vosk runtime, not a guessed marker file.
$requiredModelFiles = @(
    "am\final.mdl",
    "conf\mfcc.conf",
    "graph\HCLr.fst",
    "graph\Gr.fst"
)
foreach ($relativePath in $requiredModelFiles) {
    $requiredPath = Join-Path $targetModel $relativePath
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "The downloaded Russian model is incomplete. Missing: $relativePath"
    }
}

@"
Speech engine: Vosk $version (official Windows x64 runtime)
Language model: $modelName
Recognition: local/offline, Russian, microphone input at 16 kHz mono PCM.
Both runtime and model are bundled with this application.
Third-party project: https://github.com/alphacep/vosk-api
Vosk license: Apache-2.0. Model license and terms are distributed by the model publisher.
"@ | Set-Content -LiteralPath (Join-Path $Destination "VOSK-THIRD-PARTY-NOTICE.txt") -Encoding UTF8

if (-not (Test-Path (Join-Path $Destination "libvosk.dll") -PathType Leaf)) {
    throw "libvosk.dll was not staged."
}
Write-Host "Vosk runtime and validated Russian model staged successfully."
