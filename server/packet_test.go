package main

import (
	"encoding/binary"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestEncodePacket(t *testing.T) {
	data := make([]byte, 12)

	// 3 float32 по 4 байта каждый
	binary.LittleEndian.PutUint32(
		data[0:4],
		binary.LittleEndian.Uint32([]byte{0x00, 0x00, 0xCC, 0x41})) // 25.5
	binary.LittleEndian.PutUint32(
		data[4:8],
		binary.LittleEndian.Uint32([]byte{0x00, 0x00, 0x70, 0x42})) // 60.0
	binary.LittleEndian.PutUint32(
		data[8:12],
		binary.LittleEndian.Uint32([]byte{0x00, 0x40, 0x9C, 0x47})) // 101325.0 (600 мм рт.ст. в Паскалях)

	var packet Packet
	err := encodePacket(data, &packet)
	require.NoError(t, err)

	assert.Equal(t, float32(25.5), packet.Temperature)
	assert.Equal(t, float32(60.0), packet.Humidity)
	assert.InDelta(t, 600.0, packet.Pressure, 0.1) // Проверка с учетом погрешности
	assert.InEpsilon(t, time.Now(), packet.timestamp, float64(time.Second))
}
