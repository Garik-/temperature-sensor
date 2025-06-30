package web

import (
	"context"
	"embed"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	iofs "io/fs"
	"log/slog"
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
	EventResponse() *dataset.EventResponse
}

type eventEmitter interface {
	Subscribe() chan packet.Packet
	Unsubscribe(ch chan packet.Packet)
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

func sendResponse(w http.ResponseWriter, response *dataset.EventResponse) error {
	flusher, ok := w.(http.Flusher)
	if !ok {
		return errStreamUnsupported
	}

	if _, err := io.WriteString(w, "data: "); err != nil {
		return fmt.Errorf("error writing to client: %w", err)
	}

	encoder := json.NewEncoder(w)

	if err := encoder.Encode(response); err != nil {
		return fmt.Errorf("error encoding JSON: %w", err)
	}

	if _, err := io.WriteString(w, "\n\n"); err != nil {
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

		if err := sendResponse(w, s.EventResponse()); err != nil {
			slog.ErrorContext(ctx, "failed to send initial response", "error", err)
			http.Error(w, err.Error(), http.StatusInternalServerError)

			return
		}

		for {
			select {
			case <-ch:
				if err := sendResponse(w, s.EventResponse()); err != nil {
					slog.ErrorContext(ctx, "failed to send response", "error", err)
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

func mainHandler(fileServer http.Handler, tmpl *template.Template, s stats) http.HandlerFunc {
	etagCache := make(map[string]string)
	version := time.Now().Unix()

	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			http.Error(w, http.StatusText(http.StatusMethodNotAllowed), http.StatusMethodNotAllowed)

			return
		}

		ctx := r.Context()

		path := "public" + r.URL.Path
		if fileExists(publicFiles, path) {
			if etag, ok := etagCache[path]; ok {
				w.Header().Set("ETag", etag)

				if match := r.Header.Get("If-None-Match"); match == etag {
					w.WriteHeader(http.StatusNotModified)

					return
				}
			} else {
				etag := fmt.Sprintf(`"W/%s-%d"`, r.URL.Path, version)
				w.Header().Set("ETag", etag)
				etagCache[path] = etag
			}

			w.Header().Set("Cache-Control", "public, max-age=3600")

			r.URL.Path = path
			fileServer.ServeHTTP(w, r)

			return
		}

		jsonData, err := json.Marshal(s.EventResponse())
		if err != nil {
			slog.ErrorContext(ctx, "failed to marshal JSON data", "error", err)
			http.Error(w, err.Error(), http.StatusInternalServerError)

			return
		}

		data := struct {
			JSONData string
		}{
			JSONData: string(jsonData),
		}

		slog.DebugContext(ctx, "data", "json", data.JSONData)

		err = tmpl.Execute(w, data)
		if err != nil {
			slog.ErrorContext(ctx, "failed to execute template", "error", err)
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
