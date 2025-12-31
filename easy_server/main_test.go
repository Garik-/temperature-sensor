package main_test

import (
	"strconv"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

type payload struct {
	temperature int
	humidity    int
	pressure    int
	voltage     int
}

func parsePayloadStrings(line string, tag string, out *payload) bool {
	// Найти тег
	idx := strings.Index(line, tag+":")
	if idx == -1 {
		return false
	}

	// Всё, что после "tag:"
	data := strings.TrimSpace(line[idx+len(tag)+1:])

	// Разделить по запятой
	parts := strings.Split(data, ",")
	if len(parts) != 4 {
		return false
	}

	var err error

	// Преобразовать в int
	out.temperature, err = strconv.Atoi(parts[0])
	if err != nil {
		return false
	}

	out.humidity, err = strconv.Atoi(parts[1])
	if err != nil {
		return false
	}

	out.pressure, err = strconv.Atoi(parts[2])
	if err != nil {
		return false
	}

	out.voltage, err = strconv.Atoi(parts[3])

	return err == nil
}

func parsePayloadNoSplit(line string, tag string, out *payload) bool {
	// Найти тег
	idx := strings.Index(line, tag+": ")
	if idx == -1 {
		return false
	}

	data := line[idx+len(tag)+2:]
	start := 0
	numIdx := 0

	setField := func(n int, val int) {
		switch n {
		case 0:
			out.temperature = val
		case 1:
			out.humidity = val
		case 2:
			out.pressure = val
		case 3:
			out.voltage = val
		}
	}

	for i := 0; i <= len(data); i++ {
		if i == len(data) || data[i] == ',' {
			if start == i {
				// пустое поле
				return false
			}

			num, err := strconv.Atoi(data[start:i])
			if err != nil {
				return false
			}

			setField(numIdx, num)

			numIdx++
			start = i + 1
		}
	}

	return numIdx == 4
}

func parsePayloadNoAtoi(line string, tag string, out *[4]int) bool {
	// Найти тег
	idx := strings.Index(line, tag+": ")
	if idx == -1 {
		return false
	}

	data := []byte(line[idx+len(tag)+2:])
	numIdx := 0
	val := 0
	neg := false

	for i := 0; i <= len(data); i++ {
		var c byte
		if i < len(data) {
			c = data[i]
		} else {
			// фиктивный разделитель для последнего числа
			c = ','
		}

		switch {
		case c == '-' && !neg && val == 0:
			neg = true
		case c >= '0' && c <= '9':
			val = (val << 3) + (val << 1) + int(c-'0')
		case c == ',':
			if neg {
				val = -val
			}

			if numIdx >= len(out) {
				return false
			}

			out[numIdx] = val
			numIdx++
			val = 0
			neg = false
		default:
			return false
		}
	}

	return numIdx == len(out)
}

func TestParseString(t *testing.T) {
	tests := []struct {
		input    string
		expected bool
		payload  payload
	}{
		{"I (4041275) qf8mzr: 2314,2834,99819,32", true,
			payload{temperature: 2314, humidity: 2834, pressure: 99819, voltage: 32}},
		{"I (378) heap_init: At 3FFAE6E0 len 00001920 (6 KiB): DRAM", false, payload{}},
		{"I (4041275) qf8mzr: 2314,2834", false, payload{}},
		{"I (4041275) qf8mzr: 2314,2834,99819,123", true,
			payload{temperature: 2314, humidity: 2834, pressure: 99819, voltage: 123}},
		{"I (4041275) qf8mzr: -1,2834,99819,123", true,
			payload{temperature: -1, humidity: 2834, pressure: 99819, voltage: 123}},
		{"I (4041275) qf8mzr: abc,2834,99819", false, payload{}},
		{"Random string without tag", false, payload{}},
	}

	const tag = "qf8mzr"

	t.Run("parsePayloadStrings", func(t *testing.T) {
		for _, test := range tests {
			var out payload

			result := parsePayloadStrings(test.input, tag, &out)

			require.Equal(t, test.expected, result, "input: %q", test.input)

			if test.expected {
				assert.Equal(t, test.payload, out, "input: %q", test.input)
			}
		}
	})

	t.Run("parsePayloadNoSplit", func(t *testing.T) {
		for _, test := range tests {
			var out payload

			result := parsePayloadNoSplit(test.input, tag, &out)

			require.Equal(t, test.expected, result, "input: %q", test.input)

			if test.expected {
				assert.Equal(t, test.payload, out, "input: %q", test.input)
			}
		}
	})

	t.Run("parsePayloadNoAtoi", func(t *testing.T) {
		for _, test := range tests {
			var out [4]int

			result := parsePayloadNoAtoi(test.input, tag, &out)

			require.Equal(t, test.expected, result, "input: %q", test.input)

			if test.expected {
				assert.Equal(t, test.payload.temperature, out[0], "input: %q", test.input)
				assert.Equal(t, test.payload.humidity, out[1], "input: %q", test.input)
				assert.Equal(t, test.payload.pressure, out[2], "input: %q", test.input)
				assert.Equal(t, test.payload.voltage, out[3], "input: %q", test.input)
			}
		}
	})
}

func BenchmarkParsePayloadStrings(b *testing.B) {
	const (
		line = "I (4041275) qf8mzr: -2314,2834,99819,32"
		tag  = "qf8mzr"
	)

	var out payload

	for b.Loop() {
		parsePayloadStrings(line, tag, &out)
	}
}

func BenchmarkParsePayloadNoSplit(b *testing.B) {
	const (
		line = "I (4041275) qf8mzr: -2314,2834,99819,32"
		tag  = "qf8mzr"
	)

	var out payload

	for b.Loop() {
		parsePayloadNoSplit(line, tag, &out)
	}
}

func BenchmarkParsePayloadNoAtoi(b *testing.B) {
	const (
		line = "I (4041275) qf8mzr: -2314,2834,99819,32"
		tag  = "qf8mzr"
	)

	var out [4]int

	for b.Loop() {
		parsePayloadNoAtoi(line, tag, &out)
	}
}
