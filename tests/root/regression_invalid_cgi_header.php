<?php
// Emit valid header first to verify partial headers do not leak into gateway errors
header("X-CGI-Partial: must-not-be-forwarded");

if (isset($_GET["status"])) {
    header("Status: invalid");
} else {
    header("Invalid Header: rejected");
}

echo "must-not-be-forwarded";
