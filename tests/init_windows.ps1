# Download Ncat (bundled with Nmap)
echo "Downloading the Nmap installer. Leave all default options."
wget https://nmap.org/dist/nmap-7.97-setup.exe -O nmap.exe

# Install Ncat
Start-Process -FilePath ./nmap.exe -Wait

# Remove installer
rm nmap.exe