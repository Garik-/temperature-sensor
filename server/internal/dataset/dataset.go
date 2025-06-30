package dataset

import (
	"math"
	"slices"
	"sync"
	"time"
)

type aggregatedData struct {
	morningSum,
	daySum,
	eveningSum,
	morningCount,
	dayCount,
	eveningCount float32
}

type dailyAggregatedData map[int64]aggregatedData

type setOfData struct {
	data dailyAggregatedData
	mu   sync.RWMutex
}

func (d *setOfData) push(value float32, timestamp time.Time) {
	bod := timestamp.Truncate(24 * time.Hour).UnixMilli()
	hour := timestamp.Hour()

	d.mu.Lock()
	defer d.mu.Unlock()

	if _, exists := d.data[bod]; !exists {
		d.data[bod] = aggregatedData{}
	}

	dayData := d.data[bod]

	switch {
	case hour >= 6 && hour < 14:
		dayData.morningSum += value
		dayData.morningCount++
	case hour >= 14 && hour < 19:
		dayData.daySum += value
		dayData.dayCount++
	case hour >= 19 || hour < 6:
		dayData.eveningSum += value
		dayData.eveningCount++
	}

	d.data[bod] = dayData
}

func (d *setOfData) remove(before time.Time) {
	timestamp := before.UnixMilli()

	d.mu.Lock()
	defer d.mu.Unlock()

	for key := range d.data {
		if key < timestamp {
			delete(d.data, key)
		}
	}
}

func newSetOfData() *setOfData {
	return &setOfData{
		data: make(dailyAggregatedData),
	}
}

// [[1324508400000, 34], [1324594800000, 54] , ... , [1326236400000, 43]].
type timeSeries [][]any

func toFixed(v float32) float32 {
	return float32(math.Round(float64(v*100)) / 100)
}

func (d *setOfData) timeSeries() timeSeries {
	d.mu.RLock()
	defer d.mu.RUnlock()

	series := make(timeSeries, 0, len(d.data)*3)

	timestamps := make([]int64, 0, len(d.data))
	for timestamp := range d.data {
		timestamps = append(timestamps, timestamp)
	}

	slices.Sort(timestamps)

	for _, timestamp := range timestamps {
		bod := time.UnixMilli(timestamp)
		data := d.data[timestamp]

		if data.morningCount > 0 {
			val := []any{
				bod.Add(8 * time.Hour).UnixMilli(),
				toFixed(data.morningSum / data.morningCount),
			}

			series = append(series, val)
		}

		if data.dayCount > 0 {
			val := []any{
				bod.Add(14 * time.Hour).UnixMilli(),
				toFixed(data.daySum / data.dayCount),
			}

			series = append(series, val)
		}

		if data.eveningCount > 0 {
			val := []any{
				bod.Add(19 * time.Hour).UnixMilli(),
				toFixed(data.eveningSum / data.eveningCount),
			}

			series = append(series, val)
		}
	}

	return series
}
