package main

import (
	"context"
	"errors"
	"flag"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"temperature-sensor/internal/dataset"
	"temperature-sensor/internal/packet"
	"temperature-sensor/internal/udp"
	"temperature-sensor/internal/web"

	"golang.org/x/sync/errgroup"
)

// set from ldflags.
var (
	Version = ""
)

const (
	defaultHTTPAddr = ":8001"
	defaultUDPPort  = ":12345"
	shutdownTimeout = 2 * time.Second
	clearInterval   = 1 * 24 * time.Hour
)

func main() {
	port := flag.String("port", defaultUDPPort, "UDP server port")
	addr := flag.String("addr", defaultHTTPAddr, "HTTP server address")
	showVersion := flag.Bool("v", false, "Show version information")
	flag.Parse()

	if *showVersion {
		log.Println(Version)
		os.Exit(0)
	}

	serverUDP, err := udp.Listen(*port)
	if err != nil {
		log.Println(err)

		return
	}

	defer serverUDP.Close()

	emitter := packet.NewEventEmitter()
	defer emitter.Close()

	stats := dataset.NewStats()

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	serverHTTP, err := web.New(ctx, *addr, emitter, stats)
	if err != nil {
		log.Println(err)

		return
	}

	g, gCtx := errgroup.WithContext(ctx)

	g.Go(func() error {
		return serverUDP.Subscribe(ctx, emitter)
	})

	g.Go(func() error {
		return stats.Subscribe(gCtx, emitter)
	})

	g.Go(func() error {
		return stats.Clear(gCtx, clearInterval)
	})

	g.Go(func() error {
		log.Println("listening on " + serverHTTP.Addr)

		return serverHTTP.ListenAndServe()
	})

	g.Go(func() error {
		<-gCtx.Done()

		ctx, cancel := context.WithTimeout(ctx, shutdownTimeout)
		defer cancel()

		return serverHTTP.Shutdown(ctx)
	})

	if err := g.Wait(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Println(err)
	}
}
