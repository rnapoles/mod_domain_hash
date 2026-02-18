# Stage 1: Build 
FROM debian:bullseye-slim AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    apache2 \
    apache2-dev \
    build-essential \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy and compile the module
COPY mod_domain_hash.c /build/
RUN apxs -i -a -c mod_domain_hash.c -lcrypto

# Stage 2: Runtime 
FROM debian:bullseye-slim AS runtime

# Install only runtime dependencies
RUN apt-get update && apt-get install -y \
    apache2 \
    libssl1.1 \
    curl \
    mc \
    php \
    libapache2-mod-php \
    && rm -rf /var/lib/apt/lists/*

# Copy the compiled module and its load descriptor from the builder stage
COPY --from=builder /usr/lib/apache2/modules/mod_domain_hash.so \
                    /usr/lib/apache2/modules/mod_domain_hash.so
COPY --from=builder /etc/apache2/mods-available/domain_hash.load \
                    /etc/apache2/mods-available/domain_hash.load

# Create Apache configuration
COPY domain-hash.conf /etc/apache2/sites-available/domain-hash.conf

# Copy a test script
COPY test-module.sh /usr/local/bin/test-module.sh
RUN  chmod +x /usr/local/bin/test-module.sh
RUN  mkdir -p /var/www/{example1,example2} && mkdir -p /var/sites/{sites1,sites2}

# Enable the module and configure the site
RUN a2enmod domain_hash \
    && a2dissite 000-default \
    && a2ensite domain-hash

# Expose port 80
#EXPOSE 80

# Start Apache in foreground
CMD ["apachectl", "-D", "FOREGROUND"]