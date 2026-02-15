package packet_test

import (
	"encoding/binary"
	"math"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"temperature-sensor/internal/packet"
)

func TestEncodeUDPacket(t *testing.T) {
	exp := packet.Packet{
		Temperature: 25.5,
		Humidity:    60.0,
		Pressure:    101325.0,
		Voltage:     3.3,
	}

	bytes := make([]byte, 16)
	binary.LittleEndian.PutUint32(bytes[0:4], math.Float32bits(exp.Temperature))
	binary.LittleEndian.PutUint32(bytes[4:8], math.Float32bits(exp.Humidity))
	binary.LittleEndian.PutUint32(bytes[8:12], math.Float32bits(exp.Pressure))
	binary.LittleEndian.PutUint32(bytes[12:16], math.Float32bits(exp.Voltage))

	var pack packet.Packet

	err := packet.EncodeUDPPacket(bytes, &pack)
	require.NoError(t, err)

	assert.InEpsilon(t, exp.Temperature, pack.Temperature, 1e-6)
	assert.InEpsilon(t, exp.Humidity, pack.Humidity, 1e-6)
	assert.InEpsilon(t, exp.Pressure/133.322, pack.Pressure, 1e-6)
	assert.InEpsilon(t, exp.Voltage, pack.Voltage, 1e-6)
}

func TestEncodeMQTTPacket(t *testing.T) {
	const (
		startFlag   = 0x7E
		temperature = int16(2345)
		humidity    = uint16(5678)
		pressurePa  = uint32(101325) // Pa
		voltage     = uint16(3300)
	)

	data := make([]byte, 12)
	data[0] = startFlag

	binary.LittleEndian.PutUint16(data[1:3], uint16(temperature))
	binary.LittleEndian.PutUint16(data[3:5], humidity)
	data[5] = byte((pressurePa >> 16) & 0xff)
	data[6] = byte((pressurePa >> 8) & 0xff)
	data[7] = byte(pressurePa & 0xff)
	binary.LittleEndian.PutUint16(data[8:10], voltage)

	crc := crc16LEForTest(data[1:10])
	binary.LittleEndian.PutUint16(data[10:12], crc)

	var pack packet.Packet

	err := packet.EncodeMQTTPacket(data, &pack)
	require.NoError(t, err)

	assert.InEpsilon(t, 23.45, pack.Temperature, 1e-6)
	assert.InEpsilon(t, 56.78, pack.Humidity, 1e-6)
	assert.InEpsilon(t, float32(101325.0/133.322), pack.Pressure, 1e-6)
	assert.InEpsilon(t, 3300.0, pack.Voltage, 1e-6)
}

func TestEncodeMQTTPacketInvalidCRC(t *testing.T) {
	data := make([]byte, 12)
	data[0] = 0x7E
	// zero payload and wrong crc
	binary.LittleEndian.PutUint16(data[10:12], 0x1234)

	var pack packet.Packet

	err := packet.EncodeMQTTPacket(data, &pack)
	require.Error(t, err)
	assert.Contains(t, err.Error(), "invalid crc")
}

func crc16LEForTest(data []byte) uint16 {
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
