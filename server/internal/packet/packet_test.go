package packet_test

import (
	"encoding/binary"
	"math"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	"temperature-sensor/internal/packet"
)

func TestEncodePacket(t *testing.T) {
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
	err := packet.EncodePacket(bytes, &pack)
	require.NoError(t, err)

	assert.InEpsilon(t, exp.Temperature, pack.Temperature, 1e-6)
	assert.InEpsilon(t, exp.Humidity, pack.Humidity, 1e-6)
	assert.InEpsilon(t, exp.Pressure/133.322, pack.Pressure, 1e-6)
	assert.InEpsilon(t, exp.Voltage, pack.Voltage, 1e-6)
}
