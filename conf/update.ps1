Push-Location

if (((Split-Path -Leaf $PSScriptRoot) -eq "conf") -and (Test-Path "$PSScriptRoot/../.git")) {
    Write-Output "It appears that you're in a dev environment"

    do {
        $userInput = Read-Host "Do you want to proceed? (Y/N)"
    } while ($userInput -notmatch '^[YyNn]$')

    if ($userInput -match '^[Nn]$') {
        Write-Output "Aborting..."
        Pop-Location
        exit
    }

    # CD into Mercury root
    Set-Location -Path (Join-Path $PSScriptRoot "..")
}

# Confirm from user
do {
    Write-Output "The following operation will overwrite any changes you've made to the following:"
    Write-Output " - Configuration files (e.g. mercury.conf, mimes.conf)"
    Write-Output " - Default HTML files (e.g. ./public/index.html, ./conf/html/err.html)"
    Write-Output "SSL certs in ./conf/ssl will be saved."
    $userInput = Read-Host "Do you want to proceed? (Y/N)"
} while ($userInput -notmatch '^[YyNn]$')

if ($userInput -match '^[Nn]$') {
    Write-Output "Aborting..."
    Pop-Location
    exit
}

# Fetch latest version
$versionURL = "https://wowtravis.com/mercury/latest"
$latestVersion = [System.Text.Encoding]::UTF8.GetString( (Invoke-WebRequest -Uri $versionURL).Content ).Substring(9)

# Grab download link to new version
Write-Output "Downloading version v$latestVersion..."

$archiveDir = "Mercury v$latestVersion"
if (Test-Path $archiveDir) {
    Remove-Item -Recurse $archiveDir
}

$latestVersionUnderscored = $latestVersion.Replace(".", "_")
$archiveURL = "https://github.com/travis-heavener/mercury/releases/download/v$latestVersion/Windows_v$latestVersionUnderscored.zip"

$archivePath = "latest.zip"
if (Test-Path $archivePath) {
    Remove-Item $archivePath
}
Invoke-WebRequest -Uri $archiveURL -OutFile $archivePath

# Extract archive
Expand-Archive -Path $archivePath -DestinationPath "."
Remove-Item $archivePath

#####################################################
################## BEGIN REPLACING ##################
#####################################################

# Keep archive directory and SSL certs
Move-Item "conf/ssl/*.pem" "."

# Remove everything in the directory
$basePath = Get-Location
Get-ChildItem -Path $basePath -Force |
Where-Object { $_.Name -ne $archiveDir -and $_.Extension -ne ".pem" } |
Remove-Item -Recurse -Force

# Copy all new files except the update script
Get-ChildItem "$archiveDir" -Recurse | 
    Where-Object { $_.Name -ne "update.ps1" } |
    ForEach-Object {
        $dest = Join-Path "." $_.Name
        Move-Item $_.FullName $dest -Force
    }

# Restore SSL certs
Move-Item "*.pem" "conf/ssl/"

# Detach PS window to finish updating the update script
$scriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$archivePath = Join-Path (Get-Location) $archiveDir
$finalScript = @"
Start-Sleep -Seconds 2
if (Test-Path '$archivePath\update.ps1') {
    Move-Item -Force '$archivePath\update.ps1' '$scriptDir\update.ps1'
}
Remove-Item -Recurse -Force '$archivePath'
"@

# Launch the cleanup process
Start-Process powershell -ArgumentList "-NoProfile -Command $finalScript"

# Restore location and exit main updater
Pop-Location
exit