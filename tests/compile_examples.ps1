param(
  [string]$ArduinoCli = 'arduino-cli'
)
$ErrorActionPreference = 'Stop'
$libraryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
foreach ($target in @('esp32:esp32:esp32', 'esp32:esp32:esp32c3')) {
  foreach ($example in Get-ChildItem (Join-Path $libraryRoot 'examples') -Directory) {
    Write-Output "Compiling $target / $($example.Name)"
    & $ArduinoCli compile --fqbn $target --library $libraryRoot $example.FullName
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $target / $($example.Name)" }
  }
}

