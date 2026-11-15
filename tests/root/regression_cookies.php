<?php
header("Content-Type: text/plain; charset=UTF-8");
header("Set-Cookie: first=one");
header("Set-Cookie: second=two", false);
echo "cookies-ok";
