#!/bin/bash

set -e

REPO_FOLDER_BASE=/opt/dev
REPO_FOLDER=$REPO_FOLDER_BASE/mod_domain_hash

# Sync repo
if [ -d "$REPO_FOLDER/.git" ]
then
    cd $REPO_FOLDER
    echo "== Sync Repo =="
    echo "$REPO_FOLDER"
    git config --global --add safe.directory $REPO_FOLDER
    git fetch
    git pull
else
    echo "== Create Repo =="
    echo "$REPO_FOLDER"
    git clone https://github.com/rnapoles/mod_domain_hash $REPO_FOLDER
    cd $REPO_FOLDER
fi

chown www-data:www-data -R $REPO_FOLDER
apxs -i -a -c mod_domain_hash.c -lcrypto

echo "=============="
echo "Restart Apache"
service apache2 restart

echo "=============="
echo "List Apache Modules"
apache2ctl -M