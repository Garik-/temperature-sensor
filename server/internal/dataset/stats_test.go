package dataset //nolint:testpackage

import (
	"encoding/json"
	"math/rand"
	"testing"
	"time"

	"temperature-sensor/internal/packet"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

type mockSafePacket struct {
	data packet.Packet
}

func (m *mockSafePacket) Set(data packet.Packet) {
	m.data = data
}

func (m *mockSafePacket) Get() packet.Packet {
	return m.data
}

func TestEventResponse(t *testing.T) {
	stats := &Stats{
		temperature: newSetOfData(),
		pressure:    newSetOfData(),
		voltage:     newSetOfData(),
		packet:      &mockSafePacket{},
	}

	var mockPacket packet.Packet

	for day := range 30 {
		for hour := range 24 {
			mockPacket.Temperature = rand.Float32() * 100 //nolint:gosec
			mockPacket.Pressure = rand.Float32() * 2000   //nolint:gosec
			mockPacket.Humidity = rand.Float32() * 100    //nolint:gosec
			mockPacket.Voltage = rand.Float32() * 100     //nolint:gosec

			mockPacket.Timestamp = time.Date(2023, 10, day, hour, 0, 0, 0, time.UTC)

			stats.packet.Set(mockPacket)
			stats.temperature.push(mockPacket.Temperature, mockPacket.Timestamp)
			stats.pressure.push(mockPacket.Pressure, mockPacket.Timestamp)
			stats.voltage.push(mockPacket.Voltage, mockPacket.Timestamp)
		}
	}

	response := stats.EventResponse()

	assert.Equal(t, mockPacket, response.Current)

	assert.Len(t, response.Chart.Temperature, 90)
	assert.Len(t, response.Chart.Pressure, 90)
	assert.Len(t, response.Chart.Voltage, 90)

	b, err := json.Marshal(response)
	require.NoError(t, err)
	t.Log(string(b))
}
