package main

import (
	"context"
	"errors"
	"fmt"
	"log"
	"net"
	"time"
)

const (
	defaultUDPPort  = ":12345"
	readFromTimeout = 2 * time.Second
	maxUDPSafeSize  = 1472
)

func readFromUDP(ctx context.Context, pc net.PacketConn, emitter *EventEmitter) error {
	buf := make([]byte, maxUDPSafeSize)

	for {
		select {
		case <-ctx.Done():
			return nil
		default:
		}

		err := pc.SetReadDeadline(time.Now().Add(readFromTimeout))
		if err != nil {
			return fmt.Errorf("setReadDeadline: %w", err)
		}

		n, _, err := pc.ReadFrom(buf)
		if err != nil {
			var netErr net.Error
			if errors.As(err, &netErr) && netErr.Timeout() {
				continue
			}

			log.Printf("failed to read from UDP: %v\n", err)

			continue
		}

		var p Packet

		err = encodePacket(buf[:n], &p)
		if err != nil {
			return err
		}

		log.Printf("packet=%v", p)
		emitter.Emit(p)
	}
}
