package dataset

import (
	"context"
	"log"
	"time"

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

func (s *Stats) Clear(ctx context.Context, interval time.Duration) error {
	ticker := time.NewTicker(interval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return nil
		case now := <-ticker.C:
			log.Println("running scheduled task clear")

			sevenDaysAgo := now.AddDate(0, 0, -7)
			s.temperature.remove(sevenDaysAgo)
			s.pressure.remove(sevenDaysAgo)
		}
	}
}

func (s *Stats) Current() packet.Packet {
	return s.packet.Get()
}
