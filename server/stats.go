package main

import (
	"context"
	"sync"
)

type SafePacket struct {
	data Packet
	mu   sync.Mutex
}

func NewSafePacket() *SafePacket {
	return &SafePacket{}
}

func (sp *SafePacket) Set(data Packet) {
	sp.mu.Lock()
	defer sp.mu.Unlock()

	sp.data = data
}

func (sp *SafePacket) Get() Packet {
	sp.mu.Lock()
	defer sp.mu.Unlock()

	return sp.data
}

type Stats struct {
	temperature *SetOfData
	pressure    *SetOfData
	packet      *SafePacket
}

type Series struct {
	Temperature TimeSeries `json:"temperature"`
	Pressure    TimeSeries `json:"pressure"`
}

func newStats() *Stats {
	return &Stats{
		temperature: newSetOfData(),
		pressure:    newSetOfData(),
		packet:      NewSafePacket(),
	}
}

func (s *Stats) Subscribe(ctx context.Context, emitter *EventEmitter) error {
	ch := emitter.Subscribe()
	defer emitter.Unsubscribe(ch)

	for {
		select {
		case data := <-ch:
			s.packet.Set(data)
			s.temperature.push(data.Temperature, data.timestamp)
			s.pressure.push(data.Pressure, data.timestamp)
		case <-ctx.Done():
			return nil
		}
	}
}

func (s *Stats) Series() *Series {
	return &Series{
		Temperature: s.temperature.timeSeries(),
		Pressure:    s.pressure.timeSeries(),
	}
}

func (s *Stats) Current() Packet {
	return s.packet.Get()
}
