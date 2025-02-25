.PHONY: test run lint fix

lint:
	go run github.com/golangci/golangci-lint/cmd/golangci-lint@v1.64.5 run ./...

fix:
	go run github.com/golangci/golangci-lint/cmd/golangci-lint@v1.64.5 run --fix ./...

test:
	@go test -v -race -count=5 ./...

run:
	@go run .