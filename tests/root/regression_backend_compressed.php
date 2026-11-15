<?php
header("Content-Type: text/plain; charset=UTF-8");
header("Content-Encoding: gzip");
echo gzencode("small gzip content");
