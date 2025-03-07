package web

import (
	"context"
	"embed"
	"encoding/json"
	"errors"
	"fmt"
	iofs "io/fs"
	"log"
	"net"
	"net/http"
	"text/template"
	"time"

	"temperature-sensor/internal/dataset"
	"temperature-sensor/internal/packet"
)

const (
	readHeaderTimeout = 2 * time.Second
)

type stats interface {
	Series() *dataset.Series
	Current() packet.Packet
}

type eventEmitter interface {
	Subscribe() chan packet.Packet
	Unsubscribe(ch chan packet.Packet)
	Emit(data packet.Packet)
}

type eventResponse struct {
	Current *packet.Packet  `json:"current"`
	Chart   *dataset.Series `json:"chart"`
}

//go:embed public
var publicFiles embed.FS

//go:embed templates
var templateFiles embed.FS

func newServer(ctx context.Context, addr string) *http.Server {
	return &http.Server{
		ReadHeaderTimeout: readHeaderTimeout,
		Addr:              addr,
		BaseContext: func(_ net.Listener) context.Context {
			return ctx
		},
	}
}

func subscribeHandler(emitter eventEmitter, s stats) http.HandlerFunc {
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

		emitter.Emit(s.Current())

		ctx := r.Context()
		encoder := json.NewEncoder(w)

		for {
			select {
			case data := <-ch:
				response := eventResponse{
					Current: &data,
					Chart:   s.Series(),
				}

				if _, err := fmt.Fprintf(w, "data: "); err != nil {
					log.Printf("Error writing to client: %v\n", err)

					return
				}

				if err := encoder.Encode(response); err != nil {
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

func fileExists(fs embed.FS, path string) bool {
	_, err := fs.Open(path)

	return !errors.Is(err, iofs.ErrNotExist)
}

func mainHandler(fs http.Handler, tmpl *template.Template, s stats) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			http.Error(w, http.StatusText(http.StatusMethodNotAllowed), http.StatusMethodNotAllowed)

			return
		}

		path := "public" + r.URL.Path
		if fileExists(publicFiles, path) {
			r.URL.Path = path
			fs.ServeHTTP(w, r)

			return
		}

		current := s.Current()
		response := eventResponse{
			Current: &current,
			Chart:   s.Series(),
		}

		jsonData, err := json.Marshal(response)
		if err != nil {
			http.Error(w, http.StatusText(http.StatusInternalServerError), http.StatusInternalServerError)

			return
		}

		data := struct {
			JSONData string
		}{
			JSONData: string(jsonData),
		}

		err = tmpl.Execute(w, data)
		if err != nil {
			http.Error(w, http.StatusText(http.StatusInternalServerError), http.StatusInternalServerError)
		}
	}
}

func New(ctx context.Context, addr string, emitter eventEmitter, s stats) (*http.Server, error) {
	fs := http.FileServer(http.FS(publicFiles))

	tmpl, err := template.ParseFS(templateFiles, "templates/index.html")
	if err != nil {
		return nil, fmt.Errorf("failed to load template: %w", err)
	}

	mux := http.NewServeMux()

	srv := newServer(ctx, addr)
	srv.Handler = mux

	mux.Handle("/", mainHandler(fs, tmpl, s))
	mux.Handle("/subscribe", subscribeHandler(emitter, s))

	return srv, nil
}
