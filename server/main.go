package main

import (
	"context"
	"errors"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"temperature-sensor/internal/config"
	"temperature-sensor/internal/dataset"
	"temperature-sensor/internal/mqtt"
	"temperature-sensor/internal/packet"
	"temperature-sensor/internal/serial"
	"temperature-sensor/internal/udp"
	"temperature-sensor/internal/web"

	"golang.org/x/sync/errgroup"
)

// set from ldflags.
var (
	Version = "" //nolint:gochecknoglobals
)

const (
	shutdownTimeout = 2 * time.Second
	clearInterval   = 1 * 24 * time.Hour
)

func main() { //nolint:funlen
	cfg := config.FromFlags()

	logger := slog.New(slog.NewTextHandler(os.Stdout, &slog.HandlerOptions{
		Level: func() slog.Level {
			if cfg.Debug {
				return slog.LevelDebug
			}

			return slog.LevelInfo
		}(),
	}))

	slog.SetDefault(logger)

	if cfg.ShowVersion {
		slog.Info("version info", "version", Version)
		os.Exit(0)
	}

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	slog.InfoContext(ctx, "flags", "port", cfg.UDPServer.Port, "addr", cfg.HTTPServer.Addr, "dev", cfg.Serial.PortName,
		"tag", cfg.Serial.Tag, "udp", cfg.UDPServer.Enable, "serial", cfg.Serial.Enable, "debug", cfg.Debug)

	var (
		serverUDP     *udp.Service
		serialService *serial.Service
		mqttService   *mqtt.Service
		err           error
	)

	if cfg.UDPServer.Enable {
		serverUDP, err = udp.Listen(ctx, cfg.UDPServer.Port)
		if err != nil {
			slog.Error("failed to start UDP server", "error", err)

			return
		}

		defer serverUDP.Close()
	}

	if cfg.Serial.Enable {
		serialService = serial.New(cfg.Serial.PortName, cfg.Serial.BaudRate)
	}

	emitter := packet.NewEventEmitter()
	defer emitter.Close()

	if cfg.MQTT.Enable {
		mqttService = mqtt.New(cfg.MQTT, emitter)
	}

	stats := dataset.NewStats()

	serverHTTP, err := web.New(ctx, cfg.HTTPServer.Addr, emitter, stats)
	if err != nil {
		slog.Error("failed to create HTTP server", "error", err)

		return
	}

	g, gCtx := errgroup.WithContext(ctx)

	if cfg.UDPServer.Enable {
		g.Go(func() error {
			return serverUDP.Listen(ctx, emitter)
		})
	}

	if cfg.Serial.Enable {
		g.Go(func() error {
			return serialService.Run(gCtx, cfg.Serial.Tag, emitter)
		})
	}

	if cfg.MQTT.Enable {
		g.Go(func() error {
			return mqttService.Run(gCtx)
		})

		g.Go(func() error {
			<-gCtx.Done()

			return mqttService.Close()
		})
	}

	g.Go(func() error {
		return stats.Subscribe(gCtx, emitter)
	})

	g.Go(func() error {
		return stats.Clear(gCtx, clearInterval)
	})

	g.Go(func() error {
		slog.Info("starting HTTP server", "address", serverHTTP.Addr)

		return serverHTTP.ListenAndServe()
	})

	g.Go(func() error {
		<-gCtx.Done()

		ctx, cancel := context.WithTimeout(ctx, shutdownTimeout)
		defer cancel()

		return serverHTTP.Shutdown(ctx)
	})

	if err := g.Wait(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		slog.Error("server error", "error", err)
	}
}
