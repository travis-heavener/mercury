<?php
echo ("field1: " . $_POST["field1"] . ", file contents (" . $_FILES["field2"]["name"] . "): " . file_get_contents( $_FILES["field2"]["tmp_name"] ));