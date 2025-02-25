package main

import (
	"math"
	"sync"
	"time"
)

type AggregatedData struct {
	morningSum,
	daySum,
	eveningSum,
	morningCount,
	dayCount,
	eveningCount float32
}

type DailyAggregatedData map[int64]AggregatedData

type SetOfData struct {
	mu   sync.RWMutex
	data DailyAggregatedData
}

func (d *SetOfData) push(value float32, timestamp time.Time) {
	bod := timestamp.Truncate(24 * time.Hour).UnixMilli()
	hour := timestamp.Hour()

	d.mu.Lock()
	defer d.mu.Unlock()

	if _, exists := d.data[bod]; !exists {
		d.data[bod] = AggregatedData{}
	}

	dayData := d.data[bod]
	switch {
	case hour >= 6 && hour < 12:
		dayData.morningSum += value
		dayData.morningCount++
	case hour >= 12 && hour < 18:
		dayData.daySum += value
		dayData.dayCount++
	case hour >= 18 || hour < 6:
		dayData.eveningSum += value
		dayData.eveningCount++
	}
	d.data[bod] = dayData
}

func newSetOfData() *SetOfData {
	return &SetOfData{
		data: make(DailyAggregatedData),
	}
}

// [[1324508400000, 34], [1324594800000, 54] , ... , [1326236400000, 43]]
type TimeSeries [][]any

func toFixed(v float32) float32 {
	return float32(math.Round(float64(v*100)) / 100)
}

func (d *SetOfData) timeSeries() TimeSeries {
	d.mu.RLock()
	defer d.mu.RUnlock()

	series := TimeSeries{}

	for timestamp, data := range d.data {
		bod := time.UnixMilli(timestamp)

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
				bod.Add(20 * time.Hour).UnixMilli(),
				toFixed(data.eveningSum / data.eveningCount),
			}

			series = append(series, val)
		}
	}

	return series
}
