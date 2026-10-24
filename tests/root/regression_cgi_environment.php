<?php
header("Content-Type: text/plain");

// Verify Windows CGI retains runtime context required by PHP and OPcache
if (PHP_OS_FAMILY === "Windows") {
    $missing = [];
    if (getenv("SystemRoot") === false || getenv("SystemRoot") === "") {
        $missing[] = "SystemRoot";
    }
    if (!getenv("TEMP") && !getenv("TMP")) {
        $missing[] = "TEMP/TMP";
    }
    if ($missing) {
        http_response_code(500);
        echo "Missing Windows runtime environment: " . implode(", ", $missing);
        exit;
    }

    // Verify private CGI config is loaded and OPcache cannot cache legacy requests
    $privateIni = getenv("PHPRC");
    if ($privateIni && basename(str_replace("\\", "/", $privateIni)) === "mercury-cgi.ini") {
        $loadedIni = php_ini_loaded_file();
        $opcacheLoaded = extension_loaded("Zend OPcache");
        $opcacheEnabled = in_array(strtolower(trim((string) ini_get("opcache.enable"))), ["1", "on", "yes", "true"], true);

        // Require unloaded DLL before PHP 8.5; permit built-in OPcache when disabled
        $invalidOpcache = PHP_VERSION_ID < 80500 ? $opcacheLoaded : $opcacheEnabled;
        if (!$loadedIni || strcasecmp(
                str_replace("\\", "/", realpath($loadedIni)),
                str_replace("\\", "/", realpath($privateIni))) !== 0 || $invalidOpcache) {
            http_response_code(500);
            echo "Invalid private CGI config: PHP=" . PHP_VERSION
                . "; loaded_ini=" . ($loadedIni ?: "none")
                . "; expected_ini=" . $privateIni
                . "; opcache_loaded=" . ($opcacheLoaded ? "yes" : "no")
                . "; opcache_enabled=" . ($opcacheEnabled ? "yes" : "no");
            exit;
        }
    }
}

echo "ok";
