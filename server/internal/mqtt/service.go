package mqtt

import (
	"context"
	"encoding/hex"
	"fmt"
	"log/slog"
	"temperature-sensor/internal/config"
	"temperature-sensor/internal/packet"

	mqtt "github.com/eclipse/paho.mqtt.golang"
)

type Service struct {
	topic   string
	client  mqtt.Client
	emitter eventEmitter
}

type eventEmitter interface {
	Emit(pack packet.Packet)
}

func New(cfg config.MQTT, emitter eventEmitter) *Service {
	srv := &Service{
		topic:   cfg.Topic,
		emitter: emitter,
	}

	opts := mqtt.NewClientOptions().AddBroker(cfg.Broker).SetClientID(cfg.ClientID)
	opts.SetUsername(cfg.Username)
	opts.SetPassword(cfg.Password)
	opts.SetKeepAlive(cfg.KeepAliveDuration)
	opts.SetDefaultPublishHandler(srv.messageHandler())
	opts.SetPingTimeout(cfg.PingTimeout)
	opts.SetConnectionNotificationHandler(func(_ mqtt.Client, notification mqtt.ConnectionNotification) {
		switch n := notification.(type) {
		case mqtt.ConnectionNotificationConnected:
			slog.Debug("connected")
		case mqtt.ConnectionNotificationConnecting:
			slog.Debug("connecting", "isReconnect", n.IsReconnect, "attempt", n.Attempt)
		case mqtt.ConnectionNotificationFailed:
			slog.Debug("connection failed", "reason", n.Reason)
		case mqtt.ConnectionNotificationLost:
			slog.Debug("connection lost", "reason", n.Reason)
		case mqtt.ConnectionNotificationBroker:
			slog.Debug("broker connection", "broker", n.Broker.String())
		case mqtt.ConnectionNotificationBrokerFailed:
			slog.Debug("broker connection failed", "reason", n.Reason, "broker", n.Broker.String())
		}
	})

	srv.client = mqtt.NewClient(opts)

	return srv
}

func (s *Service) Run(ctx context.Context) error {
	if token := s.client.Connect(); token.Wait() && token.Error() != nil {
		return fmt.Errorf("connect: %w", token.Error())
	}

	if token := s.client.Subscribe(s.topic, 0, nil); token.Wait() && token.Error() != nil {
		return fmt.Errorf("subscribe: %w", token.Error())
	}

	<-ctx.Done()

	return nil
}

func (s *Service) Close() error {
	if !s.client.IsConnectionOpen() {
		return nil
	}

	if token := s.client.Unsubscribe(s.topic); token.Wait() && token.Error() != nil {
		return fmt.Errorf("unsubscribe: %w", token.Error())
	}

	s.client.Disconnect(250)

	return nil
}

func (s *Service) messageHandler() mqtt.MessageHandler {
	return func(_ mqtt.Client, msg mqtt.Message) {
		raw := msg.Payload()
		rawHex := hex.EncodeToString(raw)
		slog.Debug("mqtt payload received", "topic", msg.Topic(), "raw_hex", rawHex, "size", len(raw))

		var p packet.Packet
		if err := packet.EncodeMQTTPacket(raw, &p); err != nil {
			slog.Warn("failed to parse mqtt payload", "topic", msg.Topic(), "error", err, "size", len(raw), "raw_hex", rawHex)

			return
		}

		slog.Debug("mqtt payload parsed", "topic", msg.Topic(), "packet", p.String())
		s.emitter.Emit(p)
	}
}
