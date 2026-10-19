# The lab: the full core, charging/BSS and their datastores on one host with Docker Compose
# (CLAUDE.md "all reproducible from a single `make lab-up`"; REVIEW-5ae0ef1 DEP-04, ADR-0503).
#
#   make lab-up      generate the compose config set, then start everything (building any
#                    image that does not exist yet)
#   make lab-build   rebuild every image from the current tree (slow: each image compiles the
#                    repository in its own builder stage)
#   make lab-status  container states, and which NFs exited
#   make lab-logs    follow every service's log (S=<service> for one)
#   make lab-down    stop and remove the containers (volumes kept; lab-clean removes them too)
#   make lab-config  only regenerate deploy/docker/lab/ from config/

COMPOSE := docker compose -f deploy/docker/docker-compose.yml -f deploy/docker/docker-compose.lab.yml
S ?=

.PHONY: lab-up lab-build lab-status lab-logs lab-down lab-clean lab-config lab-config-check

lab-config:
	python3 tools/lab/gen_compose_config.py

lab-config-check:
	python3 tools/lab/gen_compose_config.py --check

lab-up: lab-config
	$(COMPOSE) up -d
	@$(MAKE) --no-print-directory lab-status

lab-build: lab-config
	$(COMPOSE) build

lab-status:
	@$(COMPOSE) ps --all --format 'table {{.Service}}\t{{.State}}\t{{.Status}}'
	@exited=$$($(COMPOSE) ps --all --status exited --format '{{.Service}} {{.ExitCode}}' | awk '$$2 != 0'); \
	if [ -n "$$exited" ]; then echo; echo "Exited with an error (make lab-logs S=<service>):"; echo "$$exited"; fi

lab-logs:
	$(COMPOSE) logs -f $(S)

lab-down:
	$(COMPOSE) down

lab-clean:
	$(COMPOSE) down -v
