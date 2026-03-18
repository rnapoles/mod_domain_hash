COMPOSE  = docker compose
SERVICE  = apache
CONTAINER = mod_domain_hash_test
IMAGE    = rnapoles:mod_domain_hash

# Enable BuildKit for cache-mount support (--mount=type=cache in Dockerfile)
export DOCKER_BUILDKIT := 1
export COMPOSE_DOCKER_CLI_BUILD := 1

.PHONY: help build build-no-cache up down restart logs shell test reload status clean save

## Default 
help:
	@echo ""
	@echo "  mod_domain_hash – available targets"
	@echo ""
	@echo "  build          Build the Docker image (uses BuildKit layer cache)"
	@echo "  save           Tag and save the image as $(IMAGE) to mod_domain_hash.tar"
	@echo "  build-no-cache  Force a full rebuild, ignoring all caches"
	@echo "  up       Start the container in detached mode"
	@echo "  up-build Build the Docker image && Start the container in detached mode"
	@echo "  down     Stop and remove the container"
	@echo "  restart  Restart the container"
	@echo "  logs     Tail Apache logs"
	@echo "  shell    Open a bash shell inside the container"
	@echo "  test     Run test-module.sh inside the container"
	@echo "  reload   Gracefully reload Apache (picks up conf changes)"
	@echo "  status   Show running containers"
	@echo "  clean    Remove image and volumes"
	@echo ""

## Lifecycle 
build:
	$(COMPOSE) build

build-no-cache:
	$(COMPOSE) build --no-cache

up:
	$(COMPOSE) up -d

up-build:
	$(COMPOSE) up -d --build

down:
	$(COMPOSE) down

restart:
	$(COMPOSE) restart $(SERVICE)

## Development 
logs:
	$(COMPOSE) logs -f $(SERVICE)

shell:
	docker exec -it $(CONTAINER) bash

test:
	docker exec $(CONTAINER) /usr/local/bin/test-module.sh

reload:
	docker exec $(CONTAINER) apachectl -k graceful

## Info 
status:
	$(COMPOSE) ps

## Cleanup 
clean:
	$(COMPOSE) down --rmi local --volumes --remove-orphans

## Image export 
save:
	docker tag $(shell $(COMPOSE) images -q $(SERVICE)) $(IMAGE)
	docker save $(IMAGE) -o mod_domain_hash.tar
	@echo "Image saved to mod_domain_hash.tar as $(IMAGE)"
