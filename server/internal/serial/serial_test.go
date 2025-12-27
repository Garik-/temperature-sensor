package serial //nolint:testpackage

import (
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

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

	tag := "qf8mzr"

	for _, test := range tests {
		var out payload

		result := parseFast(test.input, tag, &out)

		require.Equal(t, test.expected, result, "input: %q", test.input)

		if test.expected {
			assert.Equal(t, test.payload, out, "input: %q", test.input)
		}
	}
}
