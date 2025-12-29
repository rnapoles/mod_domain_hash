#!/usr/bin/env bash

# Check if $1 exists
if [[ -z "$1" ]]; then
    echo "Usage: $0 <domain>"
    exit 1
fi

BASE=/var/www
DOMAIN="${1#www.}"
HASH=$(echo -n "$DOMAIN" | sha1sum | cut -c1-7)
HASH_PATH=$(echo "$HASH" | sed 's/\(.\)/\1\//g' | sed 's/\/$//')
ROOT="$BASE/$HASH_PATH/$DOMAIN"

# If $ROOT exists, skip
if [[ -d "$ROOT" ]]; then
    echo "Already exists: $ROOT"
    exit 0
fi

mkdir -p "$ROOT"
echo "Created: $ROOT"
echo "<?php echo \$_SERVER['SERVER_NAME'] . PHP_EOL;" > $ROOT/index.php
