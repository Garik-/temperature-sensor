package main

import (
	"context"
	"errors"
	"flag"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"temperature-sensor/internal/dataset"
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
	defaultHTTPAddr  = ":8001"
	defaultUDPPort   = ":12345"
	defaultEnableUDP = false
	defaultDevice    = "/dev/tty.usbserial-58B90790011"
	defaultDeviceTag = "qf8mzr"

	shutdownTimeout = 2 * time.Second
	clearInterval   = 1 * 24 * time.Hour
)

func main() { //nolint:funlen
	port := flag.String("port", defaultUDPPort, "UDP server port")
	addr := flag.String("addr", defaultHTTPAddr, "HTTP server address")
	device := flag.String("dev", defaultDevice, "serial device path (e.g., /dev/ttyUSB0)")
	deviceTag := flag.String("tag", defaultDeviceTag, "device tag identifier")
	showVersion := flag.Bool("v", false, "show version information")
	debugMode := flag.Bool("debug", false, "enable debug mode")
	enableUDP := flag.Bool("udp", defaultEnableUDP, "enable UDP server")

	flag.Parse()

	logger := slog.New(slog.NewTextHandler(os.Stdout, &slog.HandlerOptions{
		Level: func() slog.Level {
			if *debugMode {
				return slog.LevelDebug
			}

			return slog.LevelInfo
		}(),
	}))

	slog.SetDefault(logger)

	if *showVersion {
		slog.Info("version info", "version", Version)
		os.Exit(0)
	}

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	slog.InfoContext(ctx, "flags", "port", *port, "addr", *addr, "dev", *device,
		"tag", *deviceTag, "udp", *enableUDP, "debug", *debugMode)

	var (
		serverUDP *udp.Service
		err       error
	)

	if *enableUDP {
		serverUDP, err = udp.Listen(ctx, *port)
		if err != nil {
			slog.Error("failed to start UDP server", "error", err)

			return
		}

		defer serverUDP.Close()
	}

	serialService, err := serial.Open(*device, 115200)
	if err != nil {
		slog.Error("failed to open serial port", "error", err, "device", *device)

		return
	}

	emitter := packet.NewEventEmitter()
	defer emitter.Close()

	stats := dataset.NewStats()

	serverHTTP, err := web.New(ctx, *addr, emitter, stats)
	if err != nil {
		slog.Error("failed to create HTTP server", "error", err)

		return
	}

	g, gCtx := errgroup.WithContext(ctx)

	if *enableUDP {
		g.Go(func() error {
			return serverUDP.Listen(ctx, emitter)
		})
	}

	g.Go(func() error {
		return serialService.Read(gCtx, *deviceTag, emitter)
	})

	g.Go(func() error {
		<-gCtx.Done()

		return serialService.Close()
	})

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
