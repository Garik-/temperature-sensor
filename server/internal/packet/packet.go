package packet

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"sync"
	"time"
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

type Packet struct {
	Timestamp   time.Time `json:"timestamp"`
	Temperature float32   `json:"temperature"`
	Humidity    float32   `json:"humidity"`
	Pressure    float32   `json:"pressure"`
	Voltage     float32   `json:"voltage"`
}

func PascalToMmHg(pascal float32) float32 {
	return pascal / 133.322
}

func EncodeUDPPacket(data []byte, p *Packet) error {
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

	err = binary.Read(buf, binary.LittleEndian, &p.Voltage)
	if err != nil {
		return fmt.Errorf("failed to read voltage: %w", err)
	}

	p.Pressure = PascalToMmHg(p.Pressure)
	p.Timestamp = time.Now()

	return nil
}
