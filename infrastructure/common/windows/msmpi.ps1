$ErrorActionPreference = "Stop"

$rooturl = "https://github.com/microsoft/Microsoft-MPI/releases/download"
$version = "10.1.1"
$baseurl = "$rooturl/v$version"

$tempdir    = $Env:SOURCE_PATH
$msmpisdk   = Join-Path $tempdir msmpisdk.msi
$msmpisetup = Join-Path $tempdir msmpisetup.exe

function Download-File($url, $filename) {
  foreach ($i in 1..5) {
    try {
      Write-Host "Downloading ${url}"
      Invoke-WebRequest $url -OutFile $filename
      return
    }
    catch {
      $message = $_
      Write-Warning "${message}"
      Write-Host "Download failed, retrying ..."
      Start-Sleep -Seconds $i
    }
  }
  throw "Failed to download from ${url}"
  return $null
}

Write-Host "Downloading Microsoft MPI $version"
Download-File "$baseurl/msmpisdk.msi"   $msmpisdk
Download-File "$baseurl/msmpisetup.exe" $msmpisetup

Write-Host "Installing Microsoft MPI $version"
Start-Process msiexec.exe -ArgumentList "/quiet /passive /qn /i $msmpisdk" -Wait
Start-Process $msmpisetup -ArgumentList "-unattend" -Wait

if ($Env:GITHUB_ENV) {
  Write-Host 'Adding environment variables to $GITHUB_ENV'
  $envlist = @("MSMPI_BIN", "MSMPI_INC", "MSMPI_LIB32", "MSMPI_LIB64")
  foreach ($name in $envlist) {
    $value = [Environment]::GetEnvironmentVariable($name, "Machine")
    Write-Host "$name=$value"
    Add-Content $Env:GITHUB_ENV "$name=$value"
  }
}

if ($Env:GITHUB_PATH) {
  Write-Host 'Adding $MSMPI_BIN to $GITHUB_PATH'
  $MSMPI_BIN = [Environment]::GetEnvironmentVariable("MSMPI_BIN", "Machine")
  Add-Content $Env:GITHUB_PATH $MSMPI_BIN
}  
