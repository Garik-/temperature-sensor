package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"net/http"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"golang.org/x/sync/errgroup"
)

const (
	readHeaderTimeout = 2 * time.Second
	defaultHTTPAddr   = ":8001"
	defaultUDPPort    = ":12345"
	defaultStaticDir  = "./../frontend"

	readFromTimeout = 2 * time.Second
)

func newServer(ctx context.Context, addr string) *http.Server {
	return &http.Server{
		ReadHeaderTimeout: readHeaderTimeout,
		Addr:              addr,
		BaseContext: func(_ net.Listener) context.Context {
			return ctx
		},
	}
}

func listenAndServe(g *errgroup.Group, gCtx context.Context, server *http.Server) {
	g.Go(func() error {
		log.Printf("listening on %s\n", server.Addr)
		return server.ListenAndServe()
	})
	g.Go(func() error {
		<-gCtx.Done()
		return server.Shutdown(context.Background())
	})
}

type Packet struct {
	Temperature float32 `json:"temperature"`
	Humidity    float32 `json:"humidity"`
	Pressure    float32 `json:"pressure"`
}

type EventEmitter struct {
	subscribers map[chan Packet]struct{}
	mu          sync.Mutex
}

func NewEventEmitter() *EventEmitter {
	return &EventEmitter{
		subscribers: make(map[chan Packet]struct{}),
	}
}

func (e *EventEmitter) Subscribe() chan Packet {
	e.mu.Lock()
	defer e.mu.Unlock()

	ch := make(chan Packet)
	e.subscribers[ch] = struct{}{}
	return ch
}

func (e *EventEmitter) Unsubscribe(ch chan Packet) {
	e.mu.Lock()
	defer e.mu.Unlock()

	delete(e.subscribers, ch)
	close(ch)
}

// Emit отправляет данные всем подписчикам
func (e *EventEmitter) Emit(data Packet) {
	e.mu.Lock()
	defer e.mu.Unlock()

	for ch := range e.subscribers {
		select {
		case ch <- data: // Отправляем данные в канал подписчика
		default:
			// Если канал заполнен, пропускаем подписчика
		}
	}
}

func (e *EventEmitter) Close() {
	e.mu.Lock()
	defer e.mu.Unlock()

	for ch := range e.subscribers {
		close(ch)
	}
	e.subscribers = nil
}

func encodePacket(data []byte, p *Packet) error {
	buf := bytes.NewReader(data)

	err := binary.Read(buf, binary.LittleEndian, &p.Temperature)
	if err != nil {
		return fmt.Errorf("failed to read temperature: %w", err)
	}

	err = binary.Read(buf, binary.LittleEndian, &p.Humidity)
	if err != nil {
		return fmt.Errorf("failed to read humidity: %w", err)
	}

	err = binary.Read(buf, binary.LittleEndian, &p.Pressure)
	if err != nil {
		return fmt.Errorf("failed to read pressure: %w", err)
	}

	return nil
}

func run(ctx context.Context, pc net.PacketConn, emitter *EventEmitter) error {
	buf := make([]byte, 1024)

	for {
		select {
		case <-ctx.Done():
			return nil
		default:
		}

		err := pc.SetReadDeadline(time.Now().Add(readFromTimeout))
		if err != nil {
			return err
		}

		n, _, err := pc.ReadFrom(buf)

		if err != nil {
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
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

func subscribeHandler(emitter *EventEmitter) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Expose-Headers", "Content-Type")

		w.Header().Set("Content-Type", "text/event-stream")
		w.Header().Set("Cache-Control", "no-cache")
		w.Header().Set("Connection", "keep-alive")

		flusher, ok := w.(http.Flusher)
		if !ok {
			http.Error(w, "Streaming unsupported!", http.StatusInternalServerError)
			return
		}

		ch := emitter.Subscribe()
		defer emitter.Unsubscribe(ch)

		ctx := r.Context()
		encoder := json.NewEncoder(w)

		for {
			select {
			case data := <-ch:

				if _, err := fmt.Fprintf(w, "data: "); err != nil {
					log.Printf("Error writing to client: %v\n", err)
					return
				}

				if err := encoder.Encode(data); err != nil {
					log.Printf("Error encoding JSON: %v\n", err)
					return
				}

				if _, err := fmt.Fprint(w, "\n\n"); err != nil {
					fmt.Printf("Error writing to client: %v\n", err)
					return
				}

				flusher.Flush()

			case <-ctx.Done():
				return
			}
		}
	}
}

func initServer(ctx context.Context, addr, staticDir string, emitter *EventEmitter) *http.Server {
	mux := http.NewServeMux()

	srv := newServer(ctx, addr)
	srv.Handler = mux

	fileServer := http.FileServer(http.Dir(staticDir))

	mux.Handle("/", fileServer)
	mux.Handle("/subscribe", subscribeHandler(emitter))

	return srv
}

func main() {
	log.Printf("listening UDP on %s...\n", defaultUDPPort)
	pc, err := net.ListenPacket("udp4", defaultUDPPort)
	if err != nil {
		panic(err)
	}
	defer pc.Close()

	emitter := NewEventEmitter()
	defer emitter.Close()

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	g, gCtx := errgroup.WithContext(ctx)

	g.Go(func() error {
		return run(gCtx, pc, emitter)
	})

	listenAndServe(g, gCtx, initServer(ctx, defaultHTTPAddr, defaultStaticDir, emitter))

	if err := g.Wait(); err != nil && err != http.ErrServerClosed {
		log.Println(err)
	}
}
