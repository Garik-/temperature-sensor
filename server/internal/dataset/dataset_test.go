package dataset //nolint:testpackage

import (
	"sync"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func toBod(t time.Time) int64 {
	return t.Truncate(24 * time.Hour).UnixMilli()
}

func TestPush(t *testing.T) {
	set := newSetOfData()

	for i := range 20 {
		day := i + 1

		// Test morning data
		morningTime := time.Date(2023, 10, day, 8, 0, 0, 0, time.UTC)

		bod := toBod(morningTime)

		set.push(10.5, morningTime)
		set.push(15.5, morningTime)

		assert.InEpsilon(t, float32(26), set.data[bod].morningSum, 1e-6)
		assert.InEpsilon(t, float32(2), set.data[bod].morningCount, 1e-6)

		// Test day data
		dayTime := time.Date(2023, 10, day, 14, 0, 0, 0, time.UTC)
		bod = toBod(dayTime)
		set.push(20.5, dayTime)
		set.push(25.5, dayTime)

		assert.InEpsilon(t, float32(46), set.data[bod].daySum, 1e-6)
		assert.InEpsilon(t, float32(2), set.data[bod].dayCount, 1e-6)

		// Test evening data
		eveningTime := time.Date(2023, 10, day, 20, 0, 0, 0, time.UTC)
		bod = toBod(eveningTime)
		set.push(30.5, eveningTime)
		set.push(35.5, eveningTime)

		assert.InEpsilon(t, float32(66), set.data[bod].eveningSum, 1e-6)
		assert.InEpsilon(t, float32(2), set.data[bod].eveningCount, 1e-6)
	}
}

func TestTimeSeries(t *testing.T) {
	set := newSetOfData()

	expectedSeries := make(timeSeries, 0)

	// Add data
	for i := range 20 {
		day := i + 1
		morningTime := time.Date(2023, 10, day, 8, 0, 0, 0, time.UTC)
		set.push(10.5, morningTime)
		set.push(15.5, morningTime)

		dayTime := time.Date(2023, 10, day, 14, 0, 0, 0, time.UTC)
		set.push(20.5, dayTime)
		set.push(25.5, dayTime)

		eveningTime := time.Date(2023, 10, day, 19, 0, 0, 0, time.UTC)
		set.push(30.5, eveningTime)
		set.push(35.5, eveningTime)

		val := []any{
			morningTime.UnixMilli(), toFixed(26 / 2),
		}
		expectedSeries = append(expectedSeries, val)

		val2 := []any{
			dayTime.UnixMilli(), toFixed(46 / 2),
		}
		expectedSeries = append(expectedSeries, val2)

		val3 := []any{eveningTime.UnixMilli(), toFixed(66 / 2)}

		expectedSeries = append(expectedSeries, val3)
	}

	series := set.timeSeries()

	require.Len(t, series, len(expectedSeries))

	for i, val := range series {
		if val[0] != expectedSeries[i][0] || val[1] != expectedSeries[i][1] {
			t.Errorf("Expected series[%d] to be %v, got %v", i, expectedSeries[i], val)
		}
	}
}

func BenchmarkTimeSeries(b *testing.B) {
	set := newSetOfData()

	// Add data
	for i := range 20 {
		day := i + 1
		morningTime := time.Date(2023, 10, day, 8, 0, 0, 0, time.UTC)
		set.push(10.5, morningTime)
		set.push(15.5, morningTime)

		dayTime := time.Date(2023, 10, day, 14, 0, 0, 0, time.UTC)
		set.push(20.5, dayTime)
		set.push(25.5, dayTime)

		eveningTime := time.Date(2023, 10, day, 19, 0, 0, 0, time.UTC)
		set.push(30.5, eveningTime)
		set.push(35.5, eveningTime)
	}

	b.ResetTimer()

	for b.Loop() {
		_ = set.timeSeries()
	}
}

func TestRemove(t *testing.T) {
	set := newSetOfData()

	for i := range 1000 {
		timestamp := time.Date(2023, 10, i%20, i%24, 0, 0, 0, time.UTC)
		set.push(float32(i), timestamp)
	}

	assert.Len(t, set.data, 20)
	set.remove(time.Date(2023, 10, 10, 0, 0, 0, 0, time.UTC))
	assert.Len(t, set.data, 10)
}

func TestConcurrent(t *testing.T) {
	set := newSetOfData()

	var wg sync.WaitGroup

	// Test concurrent pushes
	for i := range 1000 {
		wg.Add(1)

		go func(i int) {
			defer wg.Done()

			timestamp := time.Date(2023, 10, 1, i%24, 0, 0, 0, time.UTC)
			set.push(float32(i), timestamp)
		}(i)
	}

	// Test concurrent series
	for range 1000 {
		wg.Add(1)

		go func() {
			defer wg.Done()

			_ = set.timeSeries()
		}()
	}

	wg.Wait()

	// Verify data integrity
	if len(set.data) == 0 {
		t.Fatalf("Expected data to be populated, but it is empty")
	}
}
