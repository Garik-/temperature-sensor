.PHONY: test run lint fix build vuln tidy

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

run: tidy
	@go run .

build: tidy
	@CGO_ENABLED=0 go build -o bin/server -ldflags '-w -s' .
	@CGO_ENABLED=0 GOOS=linux GOARCH=arm64 go build -o bin/server-arm64 -ldflags '-w -s' .
	@CGO_ENABLED=0 GOOS=linux GOARCH=arm go build -o bin/server-arm -ldflags '-w -s' .
	@ls -lh bin