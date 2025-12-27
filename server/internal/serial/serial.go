package serial

import (
	"bufio"
	"context"
	"log/slog"
	"strings"
	"temperature-sensor/internal/packet"
	"time"

	"go.bug.st/serial"
)

const retryInterval = 2 * time.Second

type Service struct {
	portName string
	baudRate int
}

func New(portName string, baudRate int) *Service {
	return &Service{
		portName: portName,
		baudRate: baudRate,
	}
}

func (s *Service) Run(ctx context.Context, tag string, emitter eventEmitter) error {
	mode := &serial.Mode{
		BaudRate: s.baudRate,
	}

	for {
		select {
		case <-ctx.Done():
			return nil
		default:
		}

		port, err := serial.Open(s.portName, mode)
		if err != nil {
			slog.ErrorContext(ctx, "open failed", "port", s.portName, "err", err)
			time.Sleep(retryInterval)

			continue
		}

		err = read(ctx, port, tag, emitter)

		port.Close()

		if ctx.Err() != nil {
			return ctx.Err()
		}

		slog.Warn("serial disconnected, retrying", "err", err)
		time.Sleep(retryInterval)
	}
}

type eventEmitter interface {
	Emit(pack packet.Packet)
}

func parseInt(s string, i *int) (int, bool) {
	n := len(s)
	start := *i
	sign := 1
	val := 0

	if *i < n && s[*i] == '-' {
		sign = -1
		*i++
	}

	for *i < n {
		c := s[*i]
		if c < '0' || c > '9' {
			break
		}

		val = val*10 + int(c-'0')
		*i++
	}

	if *i == start || (*i == start+1 && sign == -1) {
		return 0, false
	}

	return val * sign, true
}

type payload struct {
	temperature int
	humidity    int
	pressure    int
	voltage     int
}

func payloadToPacket(pl payload) packet.Packet {
	return packet.Packet{
		Temperature: float32(pl.temperature) / 100.0,
		Humidity:    float32(pl.humidity) / 100.0,
		Pressure:    packet.PascalToMmHg(float32(pl.pressure)),
		Voltage:     float32(pl.voltage),
		Timestamp:   time.Now(),
	}
}

func parseFast(line string, tag string, out *payload) bool { //nolint:cyclop
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
	if !ok || i >= n || s[i] != ',' {
		return false
	}

	i++

	// parse int4
	out.voltage, ok = parseInt(s, &i)

	return ok
}

func read(ctx context.Context, port serial.Port, tag string, emitter eventEmitter) error {
	reader := bufio.NewScanner(port)
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
			slog.DebugContext(ctx, "parsed payload", "line", line, "payload", out)
			emitter.Emit(payloadToPacket(out))
		}
	}

	if err := reader.Err(); err != nil && ctx.Err() == nil {
		return err
	}

	return nil
}
