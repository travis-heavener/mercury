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

# Update tmp directory and enable OPcache
$tmpPath = [IO.Path]::GetFullPath( (Resolve-Path (Join-Path $PSScriptRoot "../tmp")).ProviderPath )
$content = Get-Content php/php.ini -Raw
$content = [System.Text.RegularExpressions.Regex]::Replace($content, ';upload_tmp_dir\s*=.*', "upload_tmp_dir = $tmpPath")
$content = [System.Text.RegularExpressions.Regex]::Replace($content, ';zend_extension\s*=\s*opcache', 'zend_extension=opcache')
$content = [System.Text.RegularExpressions.Regex]::Replace($content, ';opcache\.enable\s*=\s*1', 'opcache.enable=1')
$content = [System.Text.RegularExpressions.Regex]::Replace($content, ';opcache\.enable_cli\s*=\s*0', 'opcache.enable_cli=1')

# Resolve bundled extensions independently of working directory or system PHP
$extensionPath = (Resolve-Path (Join-Path $PSScriptRoot "../php/ext")).ProviderPath
$content += "`r`nextension_dir = `"$extensionPath`"`r`n"
Set-Content -Path php/php.ini -Value $content

# Generate FastCGI launch script for Windows
$fastcgiBat = "@echo off`r`necho Starting PHP FastCGI server on 127.0.0.1:9000...`r`n`"%~dp0php-cgi.exe`" -b 127.0.0.1:9000`r`n"
Set-Content -Path php/start_fastcgi.bat -Value $fastcgiBat

Write-Host "PHP setup complete with OPcache enabled."
Write-Host "Run php\start_fastcgi.bat or 'php\php-cgi.exe -b 127.0.0.1:9000' to start FastCGI server."

Pop-Location
