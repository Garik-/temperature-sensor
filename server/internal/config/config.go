package config

import (
	"flag"
	"time"
)

const (
	defaultHTTPAddr  = ":8001"
	defaultUDPPort   = ":12345"
	defaultEnableUDP = false

	defaultDevice       = "/dev/ttyACM0"
	defaultDeviceTag    = "qf8mzr"
	defaultEnableSerial = false
	defaultBaudRate     = 115200

	defaultEnableMQTT        = true
	defaultBroker            = "tcp://raspberrypi.local:1883"
	defaultClientID          = "go-mqtt-client"
	defaultKeepAliveDuration = 2 * time.Second
	defaultPingTimeout       = 1 * time.Second
	defaultUsername          = ""
	defaultPassword          = ""
	defaultTopic             = ""
)

type Config struct {
	ShowVersion bool
	Debug       bool

	HTTPServer HTTPServer
	UDPServer  UDPServer
	Serial     Serial
	MQTT       MQTT
}

type HTTPServer struct {
	Addr string
}

type UDPServer struct {
	Enable bool
	Port   string
}

type Serial struct {
	Enable   bool
	PortName string
	BaudRate int
	Tag      string
}

type MQTT struct {
	Enable            bool
	KeepAliveDuration time.Duration
	Broker            string
	ClientID          string
	Username          string
	Password          string
	PingTimeout       time.Duration
	Topic             string
}

func FromFlags() Config {
	cfg := Config{}

	flag.BoolVar(&cfg.ShowVersion, "app-version", false, "show version information")
	flag.BoolVar(&cfg.Debug, "app-debug", false, "enable debug mode")

	flag.StringVar(&cfg.HTTPServer.Addr, "http-addr", defaultHTTPAddr, "HTTP server address")

	flag.BoolVar(&cfg.UDPServer.Enable, "udp-enable", defaultEnableUDP, "enable UDP server")
	flag.StringVar(&cfg.UDPServer.Port, "udp-port", defaultUDPPort, "UDP server port")

	flag.BoolVar(&cfg.Serial.Enable, "serial-enable", defaultEnableSerial, "enable serial client")
	flag.StringVar(&cfg.Serial.PortName, "serial-port", defaultDevice, "serial device path (e.g., /dev/ttyUSB0)")
	flag.IntVar(&cfg.Serial.BaudRate, "serial-baud", defaultBaudRate, "serial baud rate")
	flag.StringVar(&cfg.Serial.Tag, "serial-tag", defaultDeviceTag, "device tag identifier")

	flag.BoolVar(&cfg.MQTT.Enable, "mqtt-enable", defaultEnableMQTT, "enable MQTT client")
	flag.StringVar(&cfg.MQTT.Broker, "mqtt-broker", defaultBroker, "MQTT broker URI")
	flag.StringVar(&cfg.MQTT.ClientID, "mqtt-client-id", defaultClientID, "MQTT client id")
	flag.DurationVar(&cfg.MQTT.KeepAliveDuration, "mqtt-keep-alive", defaultKeepAliveDuration, "MQTT keep alive duration")
	flag.DurationVar(&cfg.MQTT.PingTimeout, "mqtt-ping-timeout", defaultPingTimeout, "MQTT ping timeout")
	flag.StringVar(&cfg.MQTT.Username, "mqtt-username", defaultUsername, "MQTT username")
	flag.StringVar(&cfg.MQTT.Password, "mqtt-password", defaultPassword, "MQTT password")
	flag.StringVar(&cfg.MQTT.Topic, "mqtt-topic", defaultTopic, "MQTT topic")

	flag.Parse()

	return cfg
}
