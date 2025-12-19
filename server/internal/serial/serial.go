package serial

import (
	"bufio"
	"context"
	"fmt"
	"log/slog"
	"strings"
	"temperature-sensor/internal/packet"
	"time"

	"go.bug.st/serial"
)

type Service struct {
	port serial.Port
}

func Open(portName string, baudRate int) (*Service, error) {
	mode := &serial.Mode{
		BaudRate: baudRate,
	}

	slog.Info("open serial", "portName", portName, "baudRate", baudRate)

	port, err := serial.Open(portName, mode)
	if err != nil {
		return nil, fmt.Errorf("failed to open serial port: %w", err)
	}

	return &Service{
		port: port,
	}, nil
}

func (s *Service) Close() error {
	return s.port.Close()
}

type eventEmitter interface {
	Emit(pack packet.Packet)
}

func parseInt(s string, i *int) (int, bool) {
	n := len(s)
	val := 0

	for *i < n {
		c := s[*i]
		if c < '0' || c > '9' {
			break
		}

		val = val*10 + int(c-'0')
		*i++
	}

	if *i == 0 {
		return 0, false
	}

	return val, true
}

type payload struct {
	temperature int
	humidity    int
	pressure    int
}

func payloadToPacket(pl payload) packet.Packet {
	return packet.Packet{
		Temperature: float32(pl.temperature) / 100.0,
		Humidity:    float32(pl.humidity) / 100.0,
		Pressure:    packet.PascalToMmHg(float32(pl.pressure) / 100.0),
		Timestamp:   time.Now(),
	}
}

func parseFast(line string, tag string, out *payload) bool {
	// 1. быстрый фильтр по TAG
	tagPos := strings.Index(line, tag)
	if tagPos == -1 {
		return false
	}

	// 2. ищем ':' после TAG
	colon := strings.IndexByte(line[tagPos+len(tag):], ':')
	if colon == -1 {
		return false
	}

	colon += tagPos + len(tag) + 1 // абсолютная позиция

	// 3. указатель на payload
	s := line[colon:]
	n := len(s)
	i := 0

	// пропускаем пробелы
	for i < n && s[i] == ' ' {
		i++
	}

	var ok bool

	// parse int1
	out.temperature, ok = parseInt(s, &i)
	if !ok || i >= n || s[i] != ',' {
		return false
	}

	i++

	// parse int2
	out.humidity, ok = parseInt(s, &i)
	if !ok || i >= n || s[i] != ',' {
		return false
	}

	i++

	// parse int3
	out.pressure, ok = parseInt(s, &i)

	return ok
}

func (s *Service) Read(ctx context.Context, tag string, emitter eventEmitter) error {
	reader := bufio.NewScanner(s.port)
	reader.Split(bufio.ScanLines)

	var out payload

	for reader.Scan() {
		select {
		case <-ctx.Done():
			return nil
		default:
		}

		line := reader.Text()
		if line == "" {
			continue
		}

		if parseFast(line, tag, &out) {
			slog.DebugContext(ctx, line, "payload", out)
			emitter.Emit(payloadToPacket(out))
		}
	}

	if err := reader.Err(); err != nil && ctx.Err() == nil {
		return err
	}

	return nil
}
