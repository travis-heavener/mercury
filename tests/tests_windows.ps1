# CD into tests directory
Push-Location $PSScriptRoot

# Iterate over each potential HTTP request over HTTP
Get-ChildItem -Path "./requests/" -File | ForEach-Object {
    $file = $_.FullName

    # Find the matching results file
    $filename = $_.Name
    $expected_file = Join-Path "./expected/requests" $filename

    # Check if expected result file exists
    if (-not (Test-Path $expected_file)) {
        Write-Host "Missing result file: $expected_file"
        return
    }

    # Determine the expected code
    $expected_code = Get-Content $expected_file -Raw

    # Invoke each HTTP request
    $status_code = (Get-Content $file | ncat 127.0.0.1 80 | Select-String -Pattern 'HTTP/1\.1 (\d{3})' -List | Select-Object -First 1).Matches[0].Groups[1].value

    # Check if returning invalid response
    if ($status_code -ne $expected_code) {
        # Failed response
        echo "Failed ($status_code): $filename"
    }
}

# Revert to previous directory
Pop-Location