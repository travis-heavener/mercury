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
}

echo "ok";
