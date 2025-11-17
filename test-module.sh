#!/bin/bash

# Create test directory structure for 123.com
# SHA1 of "123.com" starts with 7760571...
DOMAINS=("123.com" "test.example.com" "example.org" "localhost")
for DOMAIN in "${DOMAINS[@]}"; do
  HASH=$(echo -n "$DOMAIN" | sha1sum | cut -c1-7)
  HASH_PATH=$(echo "$HASH" | sed 's/\(.\)/\1\//g' | sed 's/\/$//')
  ROOT="/var/www/$HASH_PATH/$DOMAIN"
  mkdir -p $ROOT
  echo "<?php echo \$_SERVER['SERVER_NAME'] . PHP_EOL;" > $ROOT/index.php
done

# Set proper permissions
chown -R www-data:www-data /var/www

echo "================================="
echo "Domain Hash Module Test Script"
echo "================================="
echo ""
echo "Testing 123.com (should work):"
curl -s -w "HTTP Status: %{http_code}\\n" -H "Host: 123.com" http://localhost/
echo ""
echo "---"
echo "Testing test.example.com (should work):"
curl -s -w "HTTP Status: %{http_code}\\n" -H "Host: test.example.com" http://localhost/
echo ""
echo "---"
echo "Testing example.org (should work):"
curl -s -w "HTTP Status: %{http_code}\\n"  -H "Host: example.org" http://localhost/
echo ""
echo "---"
echo "Testing nonexistent.com (should return 404):"
curl -s -w "HTTP Status: %{http_code}\\n" -H "Host: nonexistent.com" http://localhost/
echo ""
echo "---"
echo "Testing without Host header (should return default or error):"
curl -s -w "HTTP Status: %{http_code}\\n" http://localhost/
echo ""
echo "================================="
echo "Check Apache error log for details:"
echo "================================="

tail -n 20 /var/log/apache2/error.log
