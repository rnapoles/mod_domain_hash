#!/bin/bash

# Create test directory structure for 123.com
# SHA1 of "123.com" starts with 7760571...
FOLDERS=("example1" "example2")
for FOLDER in "${FOLDERS[@]}"; do
  ROOT="/var/www/$FOLDER"
  mkdir -p $ROOT
  echo "<?php echo \"$FOLDER\" . PHP_EOL;" > $ROOT/index.php
done

DOMAINS1=("123.com" "test.example.com" "example.org" "localhost")
for DOMAIN in "${DOMAINS1[@]}"; do
  HASH=$(echo -n "$DOMAIN" | sha1sum | cut -c1-7)
  HASH_PATH=$(echo "$HASH" | sed 's/\(.\)/\1\//g' | sed 's/\/$//')
  ROOT="/var/sites/sites1/$HASH_PATH/$DOMAIN"
  mkdir -p $ROOT
  echo "<?php echo \"$DOMAIN\" . PHP_EOL;" > $ROOT/index.php
done

DOMAINS2=("test1.mydomain.com" "test2.mydomain.com" "test3.mydomain.com")
for DOMAIN in "${DOMAINS2[@]}"; do
  HASH=$(echo -n "$DOMAIN" | sha1sum | cut -c1-7)
  HASH_PATH=$(echo "$HASH" | sed 's/\(.\)/\1\//g' | sed 's/\/$//')
  ROOT="/var/sites/sites2/$HASH_PATH/$DOMAIN"
  mkdir -p $ROOT
  echo "<?php echo \"$DOMAIN\" . PHP_EOL;" > $ROOT/index.php
done


# Set proper permissions
chown -R www-data:www-data /var/www
chown -R www-data:www-data /var/sites

echo "================================="
echo "Domain Hash Module Test Script"
echo "================================="

DOMAINS=("${DOMAINS1[@]}" "${DOMAINS2[@]}" "www.example1.com" "www.example2.com")
for DOMAIN in "${DOMAINS[@]}"; do
    echo ""
    echo "Testing $DOMAIN (should work):"
    curl -s -w "HTTP Status: %{http_code}\\n" -H "Host: $DOMAIN" http://localhost/
    echo ""
    echo "---"
done

for INDEX in {1..3}; do
    DOMAIN="test${INDEX}.notdomain.com"
    echo ""
    echo "Testing $DOMAIN (It shouldn't work):"
    curl -s -w "HTTP Status: %{http_code}\\n" -H "Host: $DOMAIN" http://localhost/
    echo ""
    echo "---"
done

echo "================================="
echo "Check Apache error log for details:"
echo "================================="

tail -n 20 /var/log/apache2/error.log
