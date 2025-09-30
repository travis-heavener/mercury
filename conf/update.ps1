Push-Location

# CD into project directory
Set-Location -Path "$PSScriptRoot/.."

# Confirm from user
do {
    Write-Output "The following operation will overwrite any changes you've made to the following:"
    Write-Output " - Configuration files (e.g. mercury.conf, mimes.conf)"
    Write-Output " - Default HTML files (e.g. ./public/index.html, ./conf/html/err.html)"
    Write-Output "SSL certs in ./conf/ssl will be saved."
    $userInput = Read-Host "Do you want to proceed? (Y/N)"
} while ($userInput -notmatch '^[YyNn]$')

if ($userInput -match '^[Nn]$') {
    # Abort or stop the process
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

Move-Item "$archiveDir/*" "."

# Restore SSL certs
Move-Item "*.pem" "conf/ssl/"

# Clean up
Remove-Item -Recurse $archiveDir

Pop-Location