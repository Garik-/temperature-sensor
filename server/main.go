package main

import (
	"context"
	"errors"
	"log"
	"net"
	"net/http"
	"os/signal"
	"syscall"

	"golang.org/x/sync/errgroup"
)

func main() {
	log.Printf("listening UDP on %s...\n", defaultUDPPort)

	pc, err := net.ListenPacket("udp4", defaultUDPPort)
	if err != nil {
		panic(err)
	}

	defer pc.Close()

	emitter := NewEventEmitter()
	defer emitter.Close()

	stats := newStats()

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	srv, err := initServer(ctx, defaultHTTPAddr, emitter, stats)
	if err != nil {
		log.Println(err)

		return
	}

	g, gCtx := errgroup.WithContext(ctx)

	g.Go(func() error {
		return readFromUDP(gCtx, pc, emitter)
	})

	g.Go(func() error {
		return stats.Subscribe(gCtx, emitter)
	})

	listenAndServe(g, gCtx, srv)

	if err := g.Wait(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Println(err)
	}
}
