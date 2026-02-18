COMPOSE  = docker compose
SERVICE  = apache
CONTAINER = mod_domain_hash_test

.PHONY: help build up down restart logs shell test reload status clean

## Default 
help:
	@echo ""
	@echo "  mod_domain_hash – available targets"
	@echo ""
	@echo "  build    Build (or rebuild) the Docker image"
	@echo "  up       Start the container in detached mode"
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
	$(COMPOSE) build --no-cache

up:
	$(COMPOSE) up -d

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
