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

    // Verify private CGI config is actually loaded and OPcache DLL stays unloaded
    $privateIni = getenv("PHPRC");
    if ($privateIni && basename(str_replace("\\", "/", $privateIni)) === "mercury-cgi.ini") {
        $loadedIni = php_ini_loaded_file();
        if (!$loadedIni || strcasecmp(
                str_replace("\\", "/", realpath($loadedIni)),
                str_replace("\\", "/", realpath($privateIni))) !== 0 || extension_loaded("Zend OPcache")) {
            http_response_code(500);
            echo "Private CGI config was not loaded or OPcache is still loaded";
            exit;
        }
    }
}

echo "ok";
