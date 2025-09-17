Push-Location

# CD into project directory
Set-Location -Path "$PSScriptRoot/.."

$DownloadUrl = "https://downloads.php.net/~windows/releases/php-8.4.12-Win32-vs17-x64.zip"
$ZipFile = "php-8.4.12-Win32-vs17-x64.zip"

# Remove existing PHP directory
if (Test-Path -Path "php") {
    Remove-Item -Path "php" -Recurse -Force
}

# Download zip
Invoke-WebRequest -Uri $DownloadUrl -OutFile $ZipFile -UseBasicParsing

# Extract archive
Expand-Archive -Path $ZipFile -DestinationPath "php" -Force
Remove-Item -Path $ZipFile -Force

# Copy php.ini
Copy-Item php/php.ini-development php/php.ini

# Update tmp directory
$content = Get-Content php/php.ini -Raw
$content = [System.Text.RegularExpressions.Regex]::Replace($content, ';upload_tmp_dir\s*=.*', 'upload_tmp_dir = ../tmp')
Set-Content -Path php/php.ini -Value $content

Pop-Location