package main

import (
	"encoding/binary"
	"math"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestEncodePacket(t *testing.T) {
	// Создаем структуру
	exp := Packet{
		Temperature: 25.5,
		Humidity:    60.0,
		Pressure:    101325.0,
	}

	// Преобразуем структуру в байты (Little Endian)
	bytes := make([]byte, 12) // 3 поля по 4 байта (float32)
	binary.LittleEndian.PutUint32(bytes[0:4], math.Float32bits(exp.Temperature))
	binary.LittleEndian.PutUint32(bytes[4:8], math.Float32bits(exp.Humidity))
	binary.LittleEndian.PutUint32(bytes[8:12], math.Float32bits(exp.Pressure))

	var packet Packet
	err := encodePacket(bytes, &packet)
	require.NoError(t, err)

	assert.InEpsilon(t, exp.Temperature, packet.Temperature, 1e-6)
	assert.InEpsilon(t, exp.Humidity, packet.Humidity, 1e-6)
	assert.InEpsilon(t, exp.Pressure/133.322, packet.Pressure, 1e-6)
}
