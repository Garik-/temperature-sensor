package dataset

import (
	"math"
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
	mu   sync.RWMutex
	data dailyAggregatedData
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

	series := timeSeries{}

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
