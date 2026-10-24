<?php
header("Content-Type: text/plain");
if (isset($_GET["set"])) {
    $GLOBALS["mercury_test_state"] = $_GET["set"];
}
echo $GLOBALS["mercury_test_state"] ?? "unset";
