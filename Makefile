# The lab: the full core, charging/BSS and their datastores on one host with Docker Compose
# (CLAUDE.md "all reproducible from a single `make lab-up`"; REVIEW-5ae0ef1 DEP-04, ADR-0503).
#
#   make lab-up      generate the compose config set, then start everything (building any
#                    image that does not exist yet)
#   make lab-build   rebuild every image from the current tree (slow: each image compiles the
#                    repository in its own builder stage)
#   make lab-build-fast  rebuild every image compiling the repository once, in one shared builder
#                    image that every image's runtime stage copies from (ADR-0505)
#   make lab-status  container states, and which NFs exited
#   make lab-registered  which NFs logged a successful NRF registration (ADR-0505)
#   make lab-logs    follow every service's log (S=<service> for one)
#   make lab-down    stop and remove the containers (volumes kept; lab-clean removes them too)
#   make lab-config  only regenerate deploy/docker/lab/ from config/

COMPOSE := docker compose -f deploy/docker/docker-compose.yml -f deploy/docker/docker-compose.lab.yml
S ?=

.PHONY: lab-up lab-build lab-build-fast lab-status lab-registered lab-logs lab-down lab-clean lab-config lab-config-check

lab-config:
	python3 tools/lab/gen_compose_config.py
	python3 tools/lab/gen_helm_config.py

lab-config-check:
	python3 tools/lab/gen_compose_config.py --check
	python3 tools/lab/gen_helm_config.py --check

lab-up: lab-config
	$(COMPOSE) up -d
	@$(MAKE) --no-print-directory lab-status

lab-build: lab-config
	$(COMPOSE) build

# ADR-0505: the NF Dockerfiles' builder stages differ only in --target, so one builder compiles
# every target and is passed to each image as its `builder` build context.
LAB_BUILDER_IMAGE ?= 5gc-lab-builder:local
# Compile jobs in the builder; empty = all cores. Lower it while CI runs on the same host.
LAB_BUILD_JOBS ?=

lab-build-fast: lab-config
	python3 tools/lab/gen_shared_builder.py --builder-image $(LAB_BUILDER_IMAGE)
	docker build -f build/lab-images/builder.Dockerfile -t $(LAB_BUILDER_IMAGE) \
		--build-arg CMAKE_BUILD_PARALLEL_LEVEL=$(LAB_BUILD_JOBS) .
	$(COMPOSE) -f build/lab-images/docker-compose.shared-builder.yml build

lab-status:
	@$(COMPOSE) ps --all --format 'table {{.Service}}\t{{.State}}\t{{.Status}}'
	@exited=$$($(COMPOSE) ps --all --status exited --format '{{.Service}} {{.ExitCode}}' | awk '$$2 != 0'); \
	if [ -n "$$exited" ]; then echo; echo "Exited with an error (make lab-logs S=<service>):"; echo "$$exited"; fi

# ADR-0505 (the ADR-0503 "lab-status reports container state, not registration" gap): every
# service whose lab config names an NRF, and whether its own log shows a successful NFRegister.
# From the NF's log, not an NRF query (that needs an OAuth2 token and a client certificate).
lab-registered:
	@missing=0; for f in $$(grep -l '"nrf_base_url"' deploy/docker/lab/config/*.json); do \
		s=$$(basename $$f .json); \
		if $(COMPOSE) logs --no-log-prefix $$s 2>/dev/null | grep -qE 'registered with NRF \(HTTP 20[01]\)'; then \
			echo "registered      $$s"; else echo "NOT registered  $$s"; missing=$$((missing+1)); fi; \
	done; echo; echo "$$missing service(s) not registered"; [ $$missing -eq 0 ]

lab-logs:
	$(COMPOSE) logs -f $(S)

lab-down:
	$(COMPOSE) down

lab-clean:
	$(COMPOSE) down -v
