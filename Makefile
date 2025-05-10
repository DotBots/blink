DOCKER_IMAGE ?= aabadie/dotbot:latest
DOCKER_TARGETS ?= all
PACKAGES_DIR_OPT ?=
SEGGER_DIR ?= /opt/segger
BUILD_CONFIG ?= Release

.PHONY: gateway sample clean-gateway clean-sample clean distclean docker

all: gateway

# node:
# 	@echo "\e[1mBuilding $@ application\e[0m"
# 	"$(SEGGER_DIR)/bin/emBuild" gateway/gateway-nrf52840dk.emProject -project $@ -config Release $(PACKAGES_DIR_OPT) -rebuild -verbose
# 	@echo "\e[1mDone\e[0m\n"

gateway:
	@echo "\e[1mBuilding $@ application\e[0m"
	"$(SEGGER_DIR)/bin/emBuild" app/app-gateway.emProject -project 03app_$@ -config Release $(PACKAGES_DIR_OPT) -rebuild -verbose
	@echo "\e[1mDone\e[0m\n"

# sample: node
# 	@echo "\e[1mBuilding $@ application\e[0m"
# 	"$(SEGGER_DIR)/bin/emBuild" sample/sample.emProject -project $@ -config $(BUILD_CONFIG) $(PACKAGES_DIR_OPT) -rebuild -verbose
# 	@echo "\e[1mDone\e[0m\n"

clean-gateway:
	"$(SEGGER_DIR)/bin/emBuild" gateway/gateway-nrf52840dk.emProject -config $(BUILD_CONFIG) -clean

# clean-sample:
# 	"$(SEGGER_DIR)/bin/emBuild" sample/sample.emProject -config $(BUILD_CONFIG) -clean

clean: clean-gateway clean-sample

distclean: clean

docker:
	docker run --rm -i \
		-e BUILD_TARGET="$(BUILD_TARGET)" \
		-e BUILD_CONFIG="$(BUILD_CONFIG)" \
		-e PACKAGES_DIR_OPT="-packagesdir $(SEGGER_DIR)/packages" \
		-e PROJECTS="$(PROJECTS)" \
		-e SEGGER_DIR="$(SEGGER_DIR)" \
		-v $(PWD):/dotbot $(DOCKER_IMAGE) \
		make $(DOCKER_TARGETS)
