package dataset

import (
	"context"

	"temperature-sensor/internal/packet"
)

type safePacket interface {
	Set(data packet.Packet)
	Get() packet.Packet
}

type eventEmitter interface {
	Subscribe() chan packet.Packet
	Unsubscribe(ch chan packet.Packet)
}

type Stats struct {
	temperature *setOfData
	pressure    *setOfData
	packet      safePacket
}

type Series struct {
	Temperature timeSeries `json:"temperature"`
	Pressure    timeSeries `json:"pressure"`
}

func NewStats() *Stats {
	return &Stats{
		temperature: newSetOfData(),
		pressure:    newSetOfData(),
		packet:      packet.NewSafePacket(),
	}
}

func (s *Stats) Subscribe(ctx context.Context, emitter eventEmitter) error {
	ch := emitter.Subscribe()
	defer emitter.Unsubscribe(ch)

	for {
		select {
		case data := <-ch:
			s.packet.Set(data)
			s.temperature.push(data.Temperature, data.Timestamp)
			s.pressure.push(data.Pressure, data.Timestamp)
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

func (s *Stats) Current() packet.Packet {
	return s.packet.Get()
}
