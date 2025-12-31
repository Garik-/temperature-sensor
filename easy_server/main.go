package main

import (
	"bufio"
	"context"
	"log/slog"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"github.com/VictoriaMetrics/metrics"
	"go.bug.st/serial"
)

const (
	retryInterval         = 2 * time.Second
	metricsOutputInterval = 5 * time.Minute

	defaultPortName = "/dev/ttyACM0"
	defaultTag      = "qf8mzr"
	defaultBaudRate = 115200

	defaultVictoriaURL = "http://127.0.0.1:8428/api/v1/import/prometheus"
)

var (
	temperature = metrics.NewHistogram("temperature_celsius") //nolint:gochecknoglobals
	humidity    = metrics.NewHistogram("humidity_percent")    //nolint:gochecknoglobals
	pressure    = metrics.NewHistogram("pressure_mm_hg")      //nolint:gochecknoglobals
	voltage     = metrics.NewHistogram("voltage_mv")          //nolint:gochecknoglobals
)

type payloadFields [4]int

func parsePayload(line string, tag string, out *payloadFields) bool {
	// Найти тег
	idx := strings.Index(line, tag+": ")
	if idx == -1 {
		return false
	}

	data := []byte(line[idx+len(tag)+2:])
	numIdx := 0
	val := 0
	neg := false

	for i := 0; i <= len(data); i++ {
		var c byte
		if i < len(data) {
			c = data[i]
		} else {
			// фиктивный разделитель для последнего числа
			c = ','
		}

		switch {
		case c == '-' && !neg && val == 0:
			neg = true
		case c >= '0' && c <= '9':
			val = (val << 3) + (val << 1) + int(c-'0')
		case c == ',':
			if neg {
				val = -val
			}

			if numIdx >= len(out) {
				return false
			}

			out[numIdx] = val
			numIdx++
			val = 0
			neg = false
		default:
			return false
		}
	}

	return numIdx == len(out)
}

func observe(p payloadFields) {
	temperature.Update(float64(p[0]) / 100.0)
	humidity.Update(float64(p[1]) / 100.0)
	pressure.Update(float64(p[2]) / 133.322)
	voltage.Update(float64(p[3]))
}

func read(ctx context.Context, port serial.Port, tag string) error {
	reader := bufio.NewScanner(port)
	reader.Split(bufio.ScanLines)

	var p payloadFields

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

		if parsePayload(line, tag, &p) {
			slog.DebugContext(ctx, "parsed payload", "line", line, "payload", p)

			observe(p)
		}
	}

	if err := reader.Err(); err != nil && ctx.Err() == nil {
		return err
	}

	return nil
}

func process(ctx context.Context, tag string, portName string, baudRate int) error {
	mode := &serial.Mode{
		BaudRate: baudRate,
	}

	for {
		select {
		case <-ctx.Done():
			return nil
		default:
		}

		port, err := serial.Open(portName, mode)
		if err != nil {
			slog.ErrorContext(ctx, "open failed", "port", portName, "err", err)
			time.Sleep(retryInterval)

			continue
		}

		err = read(ctx, port, tag)

		port.Close()

		if ctx.Err() != nil {
			return ctx.Err()
		}

		slog.Warn("serial disconnected, retrying", "err", err)
		time.Sleep(retryInterval)
	}
}

func main() {
	logger := slog.New(slog.NewTextHandler(os.Stdout, &slog.HandlerOptions{
		Level: slog.LevelDebug,
	}))

	slog.SetDefault(logger)

	err := metrics.InitPush(defaultVictoriaURL, metricsOutputInterval, `service_name="temperature-sensor"`, true)
	if err != nil {
		slog.Error("metrics init failed", "err", err)

		return
	}

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	err = process(ctx, defaultTag, defaultPortName, defaultBaudRate)
	if err != nil {
		slog.Error("processing failed", "err", err)
	}
}
