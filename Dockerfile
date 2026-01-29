# Dockerfile for testing mod_domain_hash Apache module
FROM debian:bullseye-slim

# Install Apache, development tools, and OpenSSL
RUN apt-get update && apt-get install -y \
    apache2 \
    apache2-dev \
    build-essential \
    libssl-dev \
    curl \
    mc \
    php \
    libapache2-mod-php \
    && rm -rf /var/lib/apt/lists/*

# Create working directory
WORKDIR /build

# Copy the module source code
COPY mod_domain_hash.c /build/

# Compile and install the module
RUN apxs -i -a -c mod_domain_hash.c -lcrypto

# Create Apache configuration
COPY domain-hash.conf /etc/apache2/sites-available/domain-hash.conf

# Copy a test script
COPY test-module.sh /usr/local/bin/test-module.sh
RUN  chmod +x /usr/local/bin/test-module.sh
RUN  mkdir -p /var/www/{example1,example2} && mkdir -p /var/sites/{sites1,sites2}

# Disable default site and enable our configuration
RUN a2dissite 000-default && a2ensite domain-hash

# Expose port 80
EXPOSE 80

# Start Apache in foreground
CMD ["apachectl", "-D", "FOREGROUND"]