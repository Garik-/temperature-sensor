// filepath: /Users/garikdjan/code/temperature-sensor/server/dataset_test.go
package main

import (
	"sync"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
)

func toBod(t time.Time) int64 {
	return t.Truncate(24 * time.Hour).UnixMilli()
}

func TestPush(t *testing.T) {
	set := newSetOfData()

	// Test morning data
	morningTime := time.Date(2023, 10, 1, 8, 0, 0, 0, time.UTC)
	bod := toBod(morningTime)

	set.push(10.5, morningTime)
	set.push(15.5, morningTime)

	assert.Equal(t, float32(26), set.data[bod].morningSum)
	assert.Equal(t, float32(2), set.data[bod].morningCount)

	// Test day data
	dayTime := time.Date(2023, 10, 1, 14, 0, 0, 0, time.UTC)
	bod = toBod(dayTime)
	set.push(20.5, dayTime)
	set.push(25.5, dayTime)

	assert.Equal(t, float32(46), set.data[bod].daySum)
	assert.Equal(t, float32(2), set.data[bod].dayCount)

	// Test evening data
	eveningTime := time.Date(2023, 10, 1, 20, 0, 0, 0, time.UTC)
	bod = toBod(eveningTime)
	set.push(30.5, eveningTime)
	set.push(35.5, eveningTime)

	assert.Equal(t, float32(66), set.data[bod].eveningSum)
	assert.Equal(t, float32(2), set.data[bod].eveningCount)
}

func TestTimeSeries(t *testing.T) {
	set := newSetOfData()

	// Add data
	morningTime := time.Date(2023, 10, 1, 8, 0, 0, 0, time.UTC)
	set.push(10.5, morningTime)
	set.push(15.5, morningTime)

	dayTime := time.Date(2023, 10, 1, 14, 0, 0, 0, time.UTC)
	set.push(20.5, dayTime)
	set.push(25.5, dayTime)

	eveningTime := time.Date(2023, 10, 1, 20, 0, 0, 0, time.UTC)
	set.push(30.5, eveningTime)
	set.push(35.5, eveningTime)

	series := set.timeSeries()

	expectedSeries := TimeSeries{
		{morningTime.Truncate(24 * time.Hour).Add(8 * time.Hour).UnixMilli(), toFixed(26 / 2)},
		{dayTime.Truncate(24 * time.Hour).Add(14 * time.Hour).UnixMilli(), toFixed(46 / 2)},
		{eveningTime.Truncate(24 * time.Hour).Add(20 * time.Hour).UnixMilli(), toFixed(66 / 2)},
	}

	assert.Len(t, series, len(expectedSeries))

	for i, val := range series {
		if val[0] != expectedSeries[i][0] || val[1] != expectedSeries[i][1] {
			t.Errorf("Expected series[%d] to be %v, got %v", i, expectedSeries[i], val)
		}
	}
}

func TestConcurrent(t *testing.T) {
	set := newSetOfData()
	var wg sync.WaitGroup

	// Test concurrent pushes
	for i := 0; i < 1000; i++ {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			timestamp := time.Date(2023, 10, 1, i%24, 0, 0, 0, time.UTC)
			set.push(float32(i), timestamp)
		}(i)
	}

	// Test concurrent series
	for i := 0; i < 1000; i++ {
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
