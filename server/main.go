package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"fmt"
	"log"
	"net"
	"os/signal"
	"strings"
	"syscall"
)

var fields = []string{"temperature", "humidity", "pressure"}

func parse(data []byte) error {

	buf := bytes.NewReader(data)

	var sb strings.Builder

	for i := range len(fields) {
		var value float32
		err := binary.Read(buf, binary.LittleEndian, &value)
		if err != nil {
			return fmt.Errorf("failed to read %s: %w", fields[i], err)
		}
		sb.WriteString(fmt.Sprintf("%s=%f ", fields[i], value))
	}
	log.Println(sb.String())
	return nil
}

func run(ctx context.Context, pc net.PacketConn) error {
	buf := make([]byte, 1024)

	for {
		select {
		case <-ctx.Done():
			return nil
		default:
		}

		n, _, err := pc.ReadFrom(buf)
		if err != nil {
			return err
		}

		err = parse(buf[:n])
		if err != nil {
			return err
		}
	}
}

func main() {
	pc, err := net.ListenPacket("udp4", ":12345")
	if err != nil {
		panic(err)
	}
	defer pc.Close()

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	go run(ctx, pc)

	<-ctx.Done()
}
