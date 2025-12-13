Push-Location

# CD into project directory
Set-Location -Path "$PSScriptRoot/.."

if ($args.Count -eq 0) {
    $PHPVersion = (
        (Invoke-WebRequest -UseBasicParsing "https://api.github.com/repos/php/php-src/releases/latest").Content | Select-String -Pattern '(?<="tag_name":"php\-)(\d+\.\d+\.\d+)(?=")' -AllMatches
    ).Matches[0].Groups[0].Value
} else {
    $PHPVersion = $args[0]
}

$DownloadUrl = "https://downloads.php.net/~windows/releases/archives/php-$PHPVersion-Win32-vs17-x64.zip"
$ZipFile = "php-$PHPVersion-Win32-vs17-x64.zip"

# Remove existing PHP directory
if (Test-Path -Path "php") {
    Remove-Item -Path "php" -Recurse -Force
}

# Download zip (silently, makes much faster)
$ProgressPreference = 'SilentlyContinue'
Invoke-WebRequest -Uri $DownloadUrl -OutFile $ZipFile -UseBasicParsing

# Extract archive
Expand-Archive -Path $ZipFile -DestinationPath "php" -Force
Remove-Item -Path $ZipFile -Force

# Copy php.ini
Copy-Item php/php.ini-development php/php.ini

# Make tmp directory
if (-not (Test-Path -Path "tmp" -PathType Container)) {
    New-Item -Path "tmp" -ItemType Directory
}

# Update tmp directory
$tmpPath = [IO.Path]::GetFullPath( (Resolve-Path (Join-Path $PSScriptRoot "../tmp")).ProviderPath )
$content = Get-Content php/php.ini -Raw
$content = [System.Text.RegularExpressions.Regex]::Replace($content, ';upload_tmp_dir\s*=.*', "upload_tmp_dir = $tmpPath")
Set-Content -Path php/php.ini -Value $content

Pop-Location