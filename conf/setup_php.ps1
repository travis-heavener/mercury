param(
    [Parameter(Position = 0)]
    [ValidatePattern("^$|^\d+\.\d+\.\d+(?:-\d+)?$")]
    [string]$phpVersion = ""
)

# Stop setup on failed downloads, extraction, or configuration writes
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

# Returns downloaded response after bounded retries
function Invoke-PhpDownload {
    param([string]$Uri, [string]$OutFile = "")

    $request = @{
        Uri = $Uri
        UseBasicParsing = $true
        TimeoutSec = 60
        ErrorAction = "Stop"
    }
    if ($OutFile) { $request.OutFile = $OutFile }

    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        try {
            return (Invoke-WebRequest @request)
        } catch {
            # Reject missing archives immediately instead of retrying permanent failures
            if ($_.Exception.Response.StatusCode -eq 404 -or $attempt -eq 3) { throw }

            Write-Warning "PHP download failed; retrying ($attempt/3)..."
            Start-Sleep -Seconds $attempt
        }
    }
}

# Resolve paths relative to project and restore caller location on failure
Push-Location -Path (Join-Path $PSScriptRoot "..")
$tempDir = $null

try {
    $releaseBase = "https://downloads.php.net/~windows/releases"

    # Read published Windows builds directly without GitHub API credentials or quotas
    $releases = (Invoke-PhpDownload -Uri "$releaseBase/releases.json").Content | ConvertFrom-Json
    $stableReleases = @($releases.PSObject.Properties | Where-Object {
        $_.Value.version -match "^\d+\.\d+\.\d+(?:-\d+)?$"
    } | Sort-Object -Property @{
        Expression = { [version]($_.Value.version -split "-")[0] }
        Descending = $true
    })

    if (-not $phpVersion) {
        if ($stableReleases.Count -eq 0) { throw "PHP Windows manifest contains no stable releases." }
        $phpVersion = $stableReleases[0].Value.version
    }

    # Preserve exact version pins, including Windows rebuild suffixes such as -1
    $release = $stableReleases | Where-Object { $_.Value.version -eq $phpVersion } |
        Select-Object -First 1
    $expectedHash = $null

    if ($release) {
        # Preserve thread-safe x64 build and discover compiler tag from manifest
        $build = $release.Value.PSObject.Properties | Where-Object {
            $_.Name -match "^ts-(?:vs|vc)\d+-x64$"
        } | Sort-Object -Property @{
            Expression = { [int]([regex]::Match($_.Name, "\d+").Value) }
            Descending = $true
        } | Select-Object -First 1
        if (-not $build) { throw "PHP $phpVersion has no thread-safe x64 Windows build." }

        $zipName = $build.Value.zip.path
        $expectedHash = $build.Value.zip.sha256
        if ($expectedHash -notmatch "^[a-fA-F0-9]{64}$") {
            throw "PHP $phpVersion manifest contains no valid SHA-256 checksum."
        }
    } else {
        # Resolve older pinned versions from official archive without guessing compiler tag
        $archiveIndex = (Invoke-PhpDownload -Uri "$releaseBase/archives/").Content
        $archivePattern = "href=`"(?<name>php-$([regex]::Escape($phpVersion))-Win32-(?:vs|vc)\d+-x64\.zip)`""
        $zipName = [regex]::Matches($archiveIndex, $archivePattern) | ForEach-Object {
            $_.Groups["name"].Value
        } | Sort-Object -Property @{
            Expression = { [int]([regex]::Match($_, "(?:vs|vc)(\d+)").Groups[1].Value) }
            Descending = $true
        } | Select-Object -First 1
        if (-not $zipName) { throw "PHP $phpVersion has no archived thread-safe x64 Windows build." }

        $releaseBase += "/archives"
    }

    # Reject unexpected filenames before building download URL
    $zipPattern = "^php-$([regex]::Escape($phpVersion))-Win32-(?:vs|vc)\d+-x64\.zip$"
    if ($zipName -notmatch $zipPattern) { throw "PHP manifest contains unexpected archive name: $zipName" }

    # Download and validate replacement before removing existing PHP installation
    $tempDir = Join-Path ([IO.Path]::GetTempPath()) ("mercury-php-" + [guid]::NewGuid().ToString("N"))
    New-Item -Path $tempDir -ItemType Directory | Out-Null
    $zipFile = Join-Path $tempDir $zipName
    Invoke-PhpDownload -Uri "$releaseBase/$zipName" -OutFile $zipFile

    if ($expectedHash -and (Get-FileHash -Path $zipFile -Algorithm SHA256).Hash -ne $expectedHash) {
        throw "PHP $phpVersion archive failed SHA-256 verification."
    }

    # Extract into temporary directory and verify required files before replacement
    $stagedPHP = Join-Path $tempDir "php"
    Expand-Archive -Path $zipFile -DestinationPath $stagedPHP -Force
    foreach ($requiredFile in @("php-cgi.exe", "php.ini-development", "ext")) {
        if (-not (Test-Path -Path (Join-Path $stagedPHP $requiredFile))) {
            throw "PHP archive is missing required file or directory: $requiredFile"
        }
    }

    if (Test-Path -Path "php") { Remove-Item -Path "php" -Recurse -Force }
    Move-Item -Path $stagedPHP -Destination "php"

    # Copy php.ini
    Copy-Item php/php.ini-development php/php.ini

    # Make tmp directory
    if (-not (Test-Path -Path "tmp" -PathType Container)) {
        New-Item -Path "tmp" -ItemType Directory | Out-Null
    }

    # Update tmp directory and enable OPcache
    $tmpPath = [IO.Path]::GetFullPath( (Resolve-Path (Join-Path $PSScriptRoot "../tmp")).ProviderPath )
    $content = Get-Content php/php.ini -Raw
    $content = [System.Text.RegularExpressions.Regex]::Replace($content, ";upload_tmp_dir\s*=.*", "upload_tmp_dir = $tmpPath")
    $content = [System.Text.RegularExpressions.Regex]::Replace($content, ";zend_extension\s*=\s*opcache", "zend_extension=opcache")
    $content = [System.Text.RegularExpressions.Regex]::Replace($content, ";opcache\.enable\s*=\s*1", "opcache.enable=1")
    $content = [System.Text.RegularExpressions.Regex]::Replace($content, ";opcache\.enable_cli\s*=\s*0", "opcache.enable_cli=1")

    # Resolve bundled extensions independently of working directory or system PHP
    $extensionPath = (Resolve-Path (Join-Path $PSScriptRoot "../php/ext")).ProviderPath
    $content += "`r`nextension_dir = `"$extensionPath`"`r`n"
    Set-Content -Path php/php.ini -Value $content

    # Keep OPcache DLL out of short-lived CGI workers, including startup and shutdown
    # Preserve other generated settings and retain OPcache in normal FastCGI config
    $legacyContent = $content.Replace("zend_extension=opcache", ";zend_extension=opcache")
    $legacyContent = $legacyContent.Replace("opcache.enable=1", "opcache.enable=0")
    Set-Content -Path php/mercury-cgi.ini -Value $legacyContent

    # Generate FastCGI launch script for Windows
    $fastcgiBat = "@echo off`r`necho Starting PHP FastCGI server on 127.0.0.1:9000...`r`n`"%~dp0php-cgi.exe`" -b 127.0.0.1:9000`r`n"
    Set-Content -Path php/start_fastcgi.bat -Value $fastcgiBat

    Write-Host "Successfully downloaded PHP $phpVersion."
    Write-Host "PHP setup complete with OPcache enabled."
    Write-Host "Mercury legacy CGI uses php\mercury-cgi.ini with OPcache unloaded."
    Write-Host "Run php\start_fastcgi.bat or 'php\php-cgi.exe -b 127.0.0.1:9000' to start FastCGI server."
} finally {
    # Remove temporary downloads and restore caller location after success or failure
    try {
        if ($tempDir -and (Test-Path -Path $tempDir)) {
            Remove-Item -Path $tempDir -Recurse -Force
        }
    } finally {
        Pop-Location
    }
}
