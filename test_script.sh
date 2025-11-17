#!/bin/bash

# Test script for mod_domain_hash module
# Run this from your host machine after starting the Docker container

echo "================================="
echo "Domain Hash Module Test Script"
echo "================================="
echo ""

# Test 123.com (should work)
echo "1. Testing 123.com (should return 200):"
echo "   Expected path: /var/www/7/7/6/0/5/7/1/123.com"
response=$(curl -s -w "\nHTTP Status: %{http_code}" -H "Host: 123.com" http://localhost:8080/)
echo "$response"
echo ""

# Test test.example.com (should work)
echo "2. Testing test.example.com (should return 200):"
echo "   Expected path: /var/www/c/8/c/1/6/a/d/test.example.com"
response=$(curl -s -w "\nHTTP Status: %{http_code}" -H "Host: test.example.com" http://localhost:8080/)
echo "$response"
echo ""

# Test example.org (should work)
echo "3. Testing example.org (should return 200):"
echo "   Expected path: /var/www/a/e/9/3/3/c/0/example.org"
response=$(curl -s -w "\nHTTP Status: %{http_code}" -H "Host: example.org" http://localhost:8080/)
echo "$response"
echo ""

# Test nonexistent domain (should return 404)
echo "4. Testing nonexistent.com (should return 404):"
response=$(curl -s -w "\nHTTP Status: %{http_code}" -H "Host: nonexistent.com" http://localhost:8080/)
echo "$response"
echo ""

# Test missing Host header
echo "5. Testing without Host header:"
response=$(curl -s -w "\nHTTP Status: %{http_code}" http://localhost:8080/)
echo "$response"
echo ""

# Test with port in Host header
echo "6. Testing 123.com:8080 (port should be stripped):"
response=$(curl -s -w "\nHTTP Status: %{http_code}" -H "Host: 123.com:8080" http://localhost:8080/)
echo "$response"
echo ""

echo "================================="
echo "Checking Apache error log:"
echo "================================="
docker exec mod_domain_hash_test tail -n 30 /var/log/apache2/error.log

echo ""
echo "================================="
echo "Verifying directory structure:"
echo "================================="
docker exec mod_domain_hash_test find /var/www -type d -name "*.com" -o -name "*.org" | sort