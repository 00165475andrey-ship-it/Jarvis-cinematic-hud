param(
    [string]$Destination = "package"
)

$ErrorActionPreference = "Stop"
$version = "0.3.45"
$modelName = "vosk-model-small-ru-0.22"
$temp = Join-Path $env:RUNNER_TEMP "jarvis-vosk"
$runtimeZip = Join-Path $temp "vosk-win64-$version.zip"
$modelZip = Join-Path $temp "$modelName.zip"
$runtimeUrl = "https://github.com/alphacep/vosk-api/releases/download/v$version/vosk-win64-$version.zip"
$modelUrl = "https://alphacephei.com/vosk/models/$modelName.zip"

New-Item -ItemType Directory -Force -Path $temp, $Destination | Out-Null
Write-Host "Downloading official Vosk Windows x64 runtime $version..."
Invoke-WebRequest -Uri $runtimeUrl -OutFile $runtimeZip
Write-Host "Downloading Russian offline model $modelName..."
Invoke-WebRequest -Uri $modelUrl -OutFile $modelZip

$runtimeDir = Join-Path $temp "runtime"
$modelDir = Join-Path $temp "model"
New-Item -ItemType Directory -Force -Path $runtimeDir, $modelDir | Out-Null
Expand-Archive -LiteralPath $runtimeZip -DestinationPath $runtimeDir -Force
Expand-Archive -LiteralPath $modelZip -DestinationPath $modelDir -Force

$dlls = Get-ChildItem -LiteralPath $runtimeDir -Filter "*.dll" -Recurse
if (-not ($dlls | Where-Object { $_.Name -ieq "libvosk.dll" })) {
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

@"
Speech engine: Vosk $version (Windows x64)
Language model: $modelName
Both are bundled locally. Speech recognition can run offline once the application integration is enabled.
Third-party project: https://github.com/alphacep/vosk-api
Vosk license: Apache-2.0. Model license and terms are distributed by the model publisher.
"@ | Set-Content -LiteralPath (Join-Path $Destination "VOSK-THIRD-PARTY-NOTICE.txt") -Encoding UTF8

if (-not (Test-Path (Join-Path $Destination "libvosk.dll"))) {
    throw "libvosk.dll was not staged."
}
if (-not (Test-Path (Join-Path $targetModel "conf\model.conf"))) {
    throw "The Russian model does not look complete."
}
Write-Host "Vosk runtime and Russian model staged successfully."
