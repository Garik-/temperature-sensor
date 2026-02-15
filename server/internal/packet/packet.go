package packet

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"sync"
	"time"
)

const (
	espNowStartFlag   = 0x7E
	espNowPayloadSize = 9
	espNowPacketSize  = 1 + espNowPayloadSize + 2
)

var (
	errInvalidESPNowPacketSize = errors.New("invalid espnow packet size")
	errInvalidESPNowStartFlag  = errors.New("invalid start flag")
	errInvalidESPNowCRC        = errors.New("invalid crc")
	errInvalidSensorData       = errors.New("invalid sensor data")
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

func (p Packet) String() string {
	return fmt.Sprintf(
		"Packet{timestamp=%s temperature=%.2f humidity=%.2f pressure=%.2f voltage=%.0f}",
		p.Timestamp.Format(time.RFC3339Nano),
		p.Temperature,
		p.Humidity,
		p.Pressure,
		p.Voltage,
	)
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

func EncodeMQTTPacket(data []byte, p *Packet) error {
	payload, err := mqttPayload(data)
	if err != nil {
		return err
	}

	wantCRC := binary.LittleEndian.Uint16(data[1+espNowPayloadSize:])
	gotCRC := crc16LE(payload)

	if gotCRC != wantCRC {
		return fmt.Errorf("%w: got=0x%04x want=0x%04x", errInvalidESPNowCRC, gotCRC, wantCRC)
	}

	return parseMQTTPayload(payload, p)
}

func mqttPayload(data []byte) ([]byte, error) {
	if len(data) != espNowPacketSize {
		return nil, fmt.Errorf("%w: got %d, want %d", errInvalidESPNowPacketSize, len(data), espNowPacketSize)
	}

	if data[0] != espNowStartFlag {
		return nil, fmt.Errorf("%w: got 0x%02x, want 0x%02x", errInvalidESPNowStartFlag, data[0], espNowStartFlag)
	}

	payload := data[1 : 1+espNowPayloadSize]

	return payload, nil
}

func parseMQTTPayload(payload []byte, p *Packet) error {
	temperature := int32(binary.LittleEndian.Uint16(payload[0:2]))

	if temperature&0x8000 != 0 {
		temperature -= 1 << 16
	}

	humidity := binary.LittleEndian.Uint16(payload[2:4])
	pressurePacked := uint32(payload[4])<<16 | uint32(payload[5])<<8 | uint32(payload[6])
	voltage := binary.LittleEndian.Uint16(payload[7:9])

	invalidRange := temperature < -5000 || temperature > 9000 || humidity > 10000 ||
		pressurePacked < 30000 || pressurePacked > 120000 || voltage < 2000 || voltage > 5000
	if invalidRange {
		return fmt.Errorf(
			"%w: temp=%d hum=%d pressurePa=%d voltage=%d",
			errInvalidSensorData,
			temperature,
			humidity,
			pressurePacked,
			voltage,
		)
	}

	p.Temperature = float32(temperature) / 100.0
	p.Humidity = float32(humidity) / 100.0
	// Firmware packs pressure in Pa into 3 bytes.
	p.Pressure = PascalToMmHg(float32(pressurePacked))
	p.Voltage = float32(voltage)
	p.Timestamp = time.Now()

	return nil
}

func crc16LE(data []byte) uint16 {
	crc := ^uint16(0xffff)

	for _, b := range data {
		crc ^= uint16(b)
		for range 8 {
			if crc&1 != 0 {
				crc = (crc >> 1) ^ 0x8408
			} else {
				crc >>= 1
			}
		}
	}

	return ^crc
}
