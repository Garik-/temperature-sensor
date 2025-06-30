.PHONY: test run lint fix build vuln tidy clean

GIT_BRANCH ?= $(shell git rev-parse --abbrev-ref HEAD)
GIT_HASH ?= $(shell git rev-parse --short HEAD)

VERSION = $(GIT_BRANCH)-$(GIT_HASH)
GO_LDFLAGS = -ldflags="-w -s -X 'main.Version=$(VERSION)'"

BINDIR = $(CURDIR)/bin
APP = temperature-sensor

ARCHS = arm64 arm

tidy:
	@go mod tidy

lint: tidy
	@go run github.com/golangci/golangci-lint/cmd/golangci-lint@v1.64.5 run ./...

fix: tidy
	@go run github.com/golangci/golangci-lint/cmd/golangci-lint@v1.64.5 run --fix ./...

vuln: tidy
	go run golang.org/x/vuln/cmd/govulncheck@latest ./...

test: tidy
	@go test -v -race -count=5 ./...

bench: tidy
	@go test -v -bench=. -benchtime=5s -benchmem -count=2 ./...

run: tidy
	@go run .

build: tidy
	@echo "Building..."
	@CGO_ENABLED=0 go build $(GO_LDFLAGS) -o $(BINDIR)/$(APP) .
	@for arch in $(ARCHS); do \
		echo "Building for $$arch..."; \
		CGO_ENABLED=0 GOOS=linux GOARCH=$$arch go build $(GO_LDFLAGS) -o $(BINDIR)/$(APP)-$$arch .; \
	done
	@ls -lh bin

deploy: build
	ssh xakep@raspberrypi.local "sudo systemctl stop temperature-sensor"
	scp bin/temperature-sensor-arm64 xakep@raspberrypi.local:/home/xakep/bin/temperature-sensor
	scp temperature-sensor.service xakep@raspberrypi.local:/home/xakep/bin/
	ssh xakep@raspberrypi.local "sudo systemctl start temperature-sensor"
	ssh xakep@raspberrypi.local "sudo systemctl status temperature-sensor"