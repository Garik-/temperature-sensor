package packet_test

import (
	"sync"
	"testing"
	"time"

	"temperature-sensor/internal/packet"

	"github.com/stretchr/testify/assert"
)

func TestEventEmitter(t *testing.T) {
	t.Run("Subscribe and Emit", func(t *testing.T) {
		emitter := packet.NewEventEmitter()
		ch := emitter.Subscribe()
		defer emitter.Unsubscribe(ch)

		var wg sync.WaitGroup

		wg.Add(1)

		done := make(chan struct{})

		exp := packet.Packet{
			Temperature: 25.5,
			Humidity:    60.0,
			Pressure:    760.0,
			Timestamp:   time.Now(),
		}

		go func() {
			defer wg.Done()

			close(done)

			pack := <-ch
			assert.InEpsilon(t, exp.Temperature, pack.Temperature, 1e-6)
			assert.InEpsilon(t, exp.Humidity, pack.Humidity, 1e-6)
			assert.InEpsilon(t, exp.Pressure, pack.Pressure, 1e-6)
		}()

		<-done
		emitter.Emit(exp)

		wg.Wait()
	})

	t.Run("Unsubscribe", func(t *testing.T) {
		emitter := packet.NewEventEmitter()
		ch := emitter.Subscribe()

		emitter.Unsubscribe(ch)

		_, ok := <-ch
		assert.False(t, ok)
	})

	t.Run("Close", func(t *testing.T) {
		emitter := packet.NewEventEmitter()
		ch1 := emitter.Subscribe()
		ch2 := emitter.Subscribe()

		emitter.Close()

		_, ok1 := <-ch1
		assert.False(t, ok1)

		_, ok2 := <-ch2
		assert.False(t, ok2)

		assert.Empty(t, emitter.Size())
	})
}
