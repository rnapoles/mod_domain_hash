# mod_domain_hash Testing Environment

This Docker setup allows you to test the `mod_domain_hash` Apache module in an isolated environment.

## Project Structure

```
.
├── Dockerfile              # Docker image definition
├── docker-compose.yml      # Docker Compose configuration
├── mod_domain_hash.c       # Apache module source code
├── test_script.sh          # Test script to verify functionality
└── README.md               # This file
```

## Prerequisites

- Docker
- Docker Compose
- curl (for testing)

## Quick Start

### 1. Build and start the container

```bash
# Build and start the container
docker-compose up -d

# Check if it's running
docker-compose ps
```

### 2. Run the tests

```bash
# Make test script executable
chmod +x test_script.sh

# Run the test script
./test_script.sh
```

### 3. Alternative: Test manually

```bash
# Test 123.com (should work - returns 200)
curl -H "Host: 123.com" http://localhost:8080/

# Test nonexistent domain (should return 404)
curl -I -H "Host: nonexistent.com" http://localhost:8080/

# Test test.example.com (should work - returns 200)
curl -H "Host: test.example.com" http://localhost:8080/
```

## Pre-configured Test Domains

The Docker container comes with these domains pre-configured:

| Domain | SHA1 (first 7 chars) | Path |
|--------|----------------------|------|
| 123.com | 7760571 | /var/www/7/7/6/0/5/7/1/123.com |
| test.example.com | c8c16ad | /var/www/c/8/c/1/6/a/d/test.example.com |
| example.org | ae933c0 | /var/www/a/e/9/3/3/c/0/example.org |

## Debugging

### View Apache error logs

```bash
docker exec mod_domain_hash_test tail -f /var/log/apache2/error.log
```

### View Apache access logs

```bash
docker exec mod_domain_hash_test tail -f /var/log/apache2/access.log
```

### Shell into the container

```bash
docker exec -it mod_domain_hash_test bash
```

### Verify directory structure

```bash
docker exec mod_domain_hash_test find /var/www -type d
```

### Check if module is loaded

```bash
docker exec mod_domain_hash_test apachectl -M | grep domain_hash
```

## Testing Additional Domains

To test a new domain, you need to:

1. Calculate its SHA1 hash (first 7 characters)
2. Create the directory structure
3. Add an index.html file

### Example: Adding newdomain.com

```bash
# Calculate SHA1 of "newdomain.com" (first 7 chars)
echo -n "newdomain.com" | sha1sum
# Output: 5f3e8d9a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e
# First 7 chars: 5f3e8d9

# Create directory structure in container
docker exec mod_domain_hash_test mkdir -p /var/www/5/f/3/e/8/d/9/newdomain.com

# Create index.html
docker exec mod_domain_hash_test bash -c 'echo "<h1>New Domain</h1>" > /var/www/5/f/3/e/8/d/9/newdomain.com/index.html'

# Test it
curl -H "Host: newdomain.com" http://localhost:8080/
```

## Expected Behavior

### Successful Request (200)
- Domain exists in the hash tree
- Returns the content from the hashed directory
- Sets VHOST_PATH environment variable

### Not Found (404)
- Domain hash directory doesn't exist
- Returns HTTP 404 error

### Edge Cases Handled
- Port numbers in Host header are stripped (e.g., `example.com:8080` → `example.com`)
- Domain names are converted to lowercase
- Missing Host header causes the module to decline (falls back to default behavior)

## Cleanup

```bash
# Stop and remove the container
docker-compose down

# Remove the image
docker-compose down --rmi all

# Remove volumes (if any)
docker-compose down -v
```

## Troubleshooting

### Module not loading

Check Apache error log:
```bash
docker exec mod_domain_hash_test tail -n 50 /var/log/apache2/error.log
```

### Compilation errors

Rebuild the container:
```bash
docker-compose down
docker-compose build --no-cache
docker-compose up -d
```

### Permission issues

Ensure www-data owns the directories:
```bash
docker exec mod_domain_hash_test chown -R www-data:www-data /var/www
```

## Performance Testing

To test with multiple concurrent requests:

```bash
# Install Apache Bench if not already installed
# apt-get install apache2-utils

# Run 1000 requests with 10 concurrent connections
ab -n 1000 -c 10 -H "Host: 123.com" http://localhost:8080/
```

## Configuration Options

The module supports these Apache directives:

- `DomainHashEnable On|Off` - Enable/disable the module
- `DomainHashBase /path/to/base` - Set the base directory (default: /var/www)

Example custom configuration:
```apache
<VirtualHost *:80>
    DomainHashEnable On
    DomainHashBase /custom/path
</VirtualHost>
```