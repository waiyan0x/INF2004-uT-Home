Message bus contract (init, publish, subscribe, bounded payloads, peer health). Application code never calls a transport directly; `transports/` hold MQTT and a loopback used for tests.
