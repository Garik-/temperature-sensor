package main

import "sync"

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

// Emit отправляет данные всем подписчикам.
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
