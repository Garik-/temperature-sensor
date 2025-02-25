package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"time"
)

type Packet struct {
	Temperature float32 `json:"temperature"`
	Humidity    float32 `json:"humidity"`
	Pressure    float32 `json:"pressure"`
	timestamp   time.Time
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

	p.Pressure /= 133.322
	p.timestamp = time.Now()

	return nil
}
