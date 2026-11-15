<?php
header("Content-Type: application/json");
echo json_encode([
    "auth_type" => $_SERVER["AUTH_TYPE"] ?? "",
    "remote_user" => $_SERVER["REMOTE_USER"] ?? "",
    "http_authorization" => $_SERVER["HTTP_AUTHORIZATION"] ?? ""
], JSON_UNESCAPED_SLASHES);
