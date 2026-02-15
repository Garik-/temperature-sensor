package mqtt

import (
	"context"
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
	logger := slog.Default()

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
		l := logger.With("component", "mqtt-connection-notifier")

		switch n := notification.(type) {
		case mqtt.ConnectionNotificationConnected:
			l.Info("connected")
		case mqtt.ConnectionNotificationConnecting:
			l.Info("connecting", "isReconnect", n.IsReconnect, "attempt", n.Attempt)
		case mqtt.ConnectionNotificationFailed:
			l.Info("connection failed", "reason", n.Reason)
		case mqtt.ConnectionNotificationLost:
			l.Info("connection lost", "reason", n.Reason)
		case mqtt.ConnectionNotificationBroker:
			l.Info("broker connection", "broker", n.Broker.String())
		case mqtt.ConnectionNotificationBrokerFailed:
			l.Info("broker connection failed", "reason", n.Reason, "broker", n.Broker.String())
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
	if token := s.client.Unsubscribe(s.topic); token.Wait() && token.Error() != nil {
		return fmt.Errorf("unsubscribe: %w", token.Error())
	}

	s.client.Disconnect(250)

	return nil
}

func (s *Service) messageHandler() mqtt.MessageHandler {
	return func(_ mqtt.Client, msg mqtt.Message) {
		var p packet.Packet
		if err := packet.EncodeMQTTPacket(msg.Payload(), &p); err != nil {
			slog.Warn("failed to parse mqtt payload", "topic", msg.Topic(), "error", err, "size", len(msg.Payload()))

			return
		}

		s.emitter.Emit(p)
	}
}
