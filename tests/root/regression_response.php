<?php
header("Content-Type: text/plain; charset=UTF-8");
if (isset($_GET["redirect"])) {
    header("Location: /small.txt", true, 302);
} elseif (isset($_GET["empty"])) {
    http_response_code(204);
} else {
    http_response_code(201);
    header("X-PHP-Test: present");
    echo "created";
}
