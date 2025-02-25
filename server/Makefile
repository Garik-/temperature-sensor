.PHONY: test run lint fix build

lint:
	go run github.com/golangci/golangci-lint/cmd/golangci-lint@v1.64.5 run ./...

fix:
	go run github.com/golangci/golangci-lint/cmd/golangci-lint@v1.64.5 run --fix ./...

test:
	@go test -v -race -count=5 ./...

run:
	@go run .

build:
	@go build -o bin/server -ldflags '-w -s' .
	@GOOS=linux GOARCH=arm64 go build -o bin/server-arm64 -ldflags '-w -s' .
	@ls -lh bin