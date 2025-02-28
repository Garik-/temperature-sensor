.PHONY: test run lint fix build vuln

lint:
	go run github.com/golangci/golangci-lint/cmd/golangci-lint@v1.64.5 run ./...


fix:
	go run github.com/golangci/golangci-lint/cmd/golangci-lint@v1.64.5 run --fix ./...


vuln:
	go run golang.org/x/vuln/cmd/govulncheck@latest ./...

test:
	@go test -v -race -count=5 ./...

run:
	@go run .

build:
	@CGO_ENABLED=0 go build -o bin/server -ldflags '-w -s' .
	@CGO_ENABLED=0 GOOS=linux GOARCH=arm64 go build -o bin/server-arm64 -ldflags '-w -s' .
	@ls -lh bin