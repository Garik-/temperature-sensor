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
}

type eventResponse struct {
	Current *packet.Packet  `json:"current"`
	Chart   *dataset.Series `json:"chart"`
}

//go:embed public
var publicFiles embed.FS

//go:embed templates
var templateFiles embed.FS

var errStreamUnsupported = errors.New("streaming unsupported")

func newServer(ctx context.Context, addr string) *http.Server {
	return &http.Server{
		ReadHeaderTimeout: readHeaderTimeout,
		Addr:              addr,
		BaseContext: func(_ net.Listener) context.Context {
			return ctx
		},
	}
}

func sendResponse(w http.ResponseWriter, response *eventResponse) error {
	flusher, ok := w.(http.Flusher)
	if !ok {
		return errStreamUnsupported
	}

	if _, err := fmt.Fprintf(w, "data: "); err != nil {
		return fmt.Errorf("error writing to client: %w", err)
	}

	encoder := json.NewEncoder(w)

	if err := encoder.Encode(response); err != nil {
		return fmt.Errorf("error encoding JSON: %w", err)
	}

	if _, err := fmt.Fprint(w, "\n\n"); err != nil {
		return fmt.Errorf("error writing to client: %w", err)
	}

	flusher.Flush()

	return nil
}

func subscribeHandler(emitter eventEmitter, s stats) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Expose-Headers", "Content-Type")

		w.Header().Set("Content-Type", "text/event-stream")
		w.Header().Set("Cache-Control", "no-cache")
		w.Header().Set("Connection", "keep-alive")

		ch := emitter.Subscribe()
		defer emitter.Unsubscribe(ch)

		ctx := r.Context()
		current := s.Current()

		if err := sendResponse(w, &eventResponse{
			Current: &current,
			Chart:   s.Series(),
		}); err != nil {
			log.Println(err.Error())
			http.Error(w, err.Error(), http.StatusInternalServerError)

			return
		}

		for {
			select {
			case data := <-ch:
				if err := sendResponse(w, &eventResponse{
					Current: &data,
					Chart:   s.Series(),
				}); err != nil {
					log.Println(err.Error())
					http.Error(w, err.Error(), http.StatusInternalServerError)

					return
				}
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
			log.Println(err.Error())

			http.Error(w, err.Error(), http.StatusInternalServerError)

			return
		}

		data := struct {
			JSONData string
		}{
			JSONData: string(jsonData),
		}

		err = tmpl.Execute(w, data)
		if err != nil {
			log.Println(err.Error())
			http.Error(w, err.Error(), http.StatusInternalServerError)
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
