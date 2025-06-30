package udp

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"net"
	"time"

	"temperature-sensor/internal/packet"
)

const (
	readFromTimeout = 2 * time.Second
	maxUDPSafeSize  = 1472
)

type Service struct {
	pc net.PacketConn
}

func Listen(port string) (*Service, error) {
	slog.Info("listening UDP", "port", port)

	pc, err := net.ListenPacket("udp4", port)
	if err != nil {
		return nil, fmt.Errorf("listenPacket: %w", err)
	}

	return &Service{
		pc: pc,
	}, nil
}

func (s *Service) Close() error {
	if s.pc == nil {
		panic("init packet connection")
	}

	return s.pc.Close() //nolint:wrapcheck
}

type eventEmitter interface {
	Emit(pack packet.Packet)
}

func (s *Service) Listen(ctx context.Context, emitter eventEmitter) error {
	buf := make([]byte, maxUDPSafeSize)

	for {
		select {
		case <-ctx.Done():
			return nil
		default:
		}

		err := s.pc.SetReadDeadline(time.Now().Add(readFromTimeout))
		if err != nil {
			return fmt.Errorf("setReadDeadline: %w", err)
		}

		n, _, err := s.pc.ReadFrom(buf)
		if err != nil {
			var netErr net.Error
			if errors.As(err, &netErr) && netErr.Timeout() {
				continue
			}

			slog.ErrorContext(ctx, "failed to read from UDP", "error", err)

			continue
		}

		var p packet.Packet

		err = packet.EncodePacket(buf[:n], &p)
		if err != nil {
			return fmt.Errorf("encodePacket: %w", err)
		}

		slog.InfoContext(ctx, "received packet",
			"temperature", p.Temperature,
			"humidity", p.Humidity,
			"pressure", p.Pressure,
			"voltage", p.Voltage,
			"timestamp", p.Timestamp)

		emitter.Emit(p)
	}
}
