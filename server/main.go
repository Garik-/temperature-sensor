package main

import (
	"context"
	"errors"
	"flag"
	"log"
	"net"
	"net/http"
	"os/signal"
	"syscall"

	"golang.org/x/sync/errgroup"
)

const (
	defaultHTTPAddr = ":8001"
	defaultUDPPort  = ":12345"
)

func main() {
	port := flag.String("port", defaultUDPPort, "UDP server port")
	addr := flag.String("addr", defaultHTTPAddr, "HTTP Server address")
	flag.Parse()

	log.Println("listening UDP on " + *port)

	pc, err := net.ListenPacket("udp4", *port)
	if err != nil {
		panic(err)
	}

	defer pc.Close()

	emitter := NewEventEmitter()
	defer emitter.Close()

	stats := newStats()

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	srv, err := initServer(ctx, *addr, emitter, stats)
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

	g.Go(func() error {
		log.Println("listening on " + srv.Addr)

		return srv.ListenAndServe()
	})

	g.Go(func() error {
		<-gCtx.Done()

		ctx, cancel := context.WithTimeout(ctx, shutdownTimeout)
		defer cancel()

		return srv.Shutdown(ctx)
	})

	if err := g.Wait(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Println(err)
	}
}
