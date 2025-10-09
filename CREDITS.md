# Credits

## Source Libraries

Mercury is made possible with the following third-party libraries:

- **Zlib** — [Zlib License](/licenses/zlib_LICENSE.txt) — [Source](https://github.com/madler/zlib)
- **PugiXML** — [MIT License](/licenses/PugiXML_LICENSE.txt) — [Source](https://github.com/zeux/pugixml)
- **OpenSSL** — [Apache 2.0 License](/licenses/OpenSSL_apache-license-2.0.txt) — [Source](https://github.com/openssl/openssl)
- **Google Brotli Compression Library** — [MIT License](/licenses/Brotli_LICENSE.txt) — [Source](https://github.com/google/brotli)
- **Zstandard** — [Zstandard License](/licenses/Zstandard_LICENSE.txt) — [Source](https://github.com/facebook/zstd)

---

*Mercury includes and statically links these libraries where applicable.
All third-party licenses are included in the `/licenses/` directory.*

## Additional Software

Windows versions of Mercury (v0.15.0 and later) include a Powershell script (conf/setup_php.ps1) to download and configure PHP 8.4.12.

Mercury does **not** bundle or redistribute PHP binaries directly, but because PHP is required for some runtime functionality, its license is provided for reference:

- **PHP 8.4.12** — [PHP License v3.01](/licenses/PHP_license.txt) — [Source](https://www.php.net/)

---

*Mercury does not modify PHP in any way and is not affiliated with the PHP Group.
All third-party licenses are included in the `/licenses/` directory.*