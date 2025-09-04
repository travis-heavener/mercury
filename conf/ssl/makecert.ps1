Push-Location

#
# Note: Update this path below to point to where openssl.exe is installed on your system
#   Windows copies of Git should come installed with OpenSSL (see C:/Program Files/Git/usr/bin/openssl.exe)
#
$OPENSSL_PATH="C:/Program Files/Git/usr/bin/openssl.exe"

# CD into ssl directory
Set-Location -Path $PSScriptRoot

# Make cert
& "$OPENSSL_PATH" req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -sha256 -days 365 -nodes

Pop-Location