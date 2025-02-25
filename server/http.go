package main

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

	"golang.org/x/sync/errgroup"
)

const (
	readHeaderTimeout = 2 * time.Second
	defaultHTTPAddr   = ":8001"
	defaultStaticDir  = "./../frontend"
)

type EventResponse struct {
	Current *Packet `json:"current"`
	Chart   *Series `json:"chart"`
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

func subscribeHandler(emitter *EventEmitter, s *Stats) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			http.Error(w, http.StatusText(http.StatusMethodNotAllowed), http.StatusMethodNotAllowed)

			return
		}

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
				response := EventResponse{
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

func mainHandler(fs http.Handler, tmpl *template.Template, s *Stats) http.HandlerFunc {
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
		response := EventResponse{
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

		tmpl.Execute(w, data)
	}
}

func initServer(ctx context.Context, addr string, emitter *EventEmitter, s *Stats) (*http.Server, error) {
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
