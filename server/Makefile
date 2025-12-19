.PHONY: test run lint fix build vuln tidy clean

GIT_BRANCH ?= $(shell git rev-parse --abbrev-ref HEAD)
GIT_HASH ?= $(shell git rev-parse --short HEAD)

VERSION = $(GIT_BRANCH)-$(GIT_HASH)
GO_LDFLAGS = -ldflags="-w -s -X 'main.Version=$(VERSION)'"

BINDIR = $(CURDIR)/bin
DEVBINDIR = $(CURDIR)/devbin
APP = temperature-sensor

ARCHS = arm64 arm


LINT_VERSION ?= v2.6.2
LINT_BIN := $(DEVBINDIR)/golangci-lint

tidy:
	@go mod tidy

lint: $(LINT_BIN)
	$(LINT_BIN) run ./...

fix: $(LINT_BIN)
	$(LINT_BIN) run --fix ./...

$(LINT_BIN):
	@echo "Installing golangci-lint $(LINT_VERSION)..."
	@mkdir -p $(DEVBINDIR)
	curl -sSfL https://raw.githubusercontent.com/golangci/golangci-lint/master/install.sh | \
		sh -s -- -b $(DEVBINDIR) $(LINT_VERSION)

vuln: tidy
	go run golang.org/x/vuln/cmd/govulncheck@latest ./...

test: tidy
	@go test -v -race -count=5 ./...

bench: tidy
	@go test -v -bench=. -benchtime=5s -benchmem -count=2 ./...

run: tidy
	@go run . -debug

build: tidy
	@echo "Building..."
	@go build $(GO_LDFLAGS) -o $(BINDIR)/$(APP) .
	@for arch in $(ARCHS); do \
		echo "Building for $$arch..."; \
		GOOS=linux GOARCH=$$arch go build $(GO_LDFLAGS) -o $(BINDIR)/$(APP)-$$arch .; \
	done
	@ls -lh bin

deploy: build
	ssh xakep@raspberrypi.local "sudo systemctl stop temperature-sensor"
	scp bin/temperature-sensor-arm64 xakep@raspberrypi.local:/home/xakep/bin/temperature-sensor
	scp temperature-sensor.service xakep@raspberrypi.local:/home/xakep/bin/
	ssh xakep@raspberrypi.local "sudo systemctl start temperature-sensor"
	ssh xakep@raspberrypi.local "sudo systemctl status temperature-sensor"