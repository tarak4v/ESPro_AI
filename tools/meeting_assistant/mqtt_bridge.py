"""
MQTT Bridge — Handles communication between watch and desktop.

Topics:
    State (desktop → watch):   espro/<device>/meeting/state/<key>
    Commands (watch → desktop): espro/<device>/meeting/cmd/<key>
    Notes (desktop → watch):   espro/<device>/meeting/notes/<key>
"""

import logging
import threading
import uuid

try:
    import paho.mqtt.client as mqtt
except ImportError:
    mqtt = None

log = logging.getLogger("mqtt")


class MQTTBridge:
    """MQTT communication bridge between watch and desktop."""

    def __init__(self, config: dict, command_callback):
        """
        config: {"broker": "...", "port": 1883, "username": "", "password": "", "device_name": "..."}
        command_callback(cmd: str, value: str): called when watch sends a command
        """
        self.broker = config.get("broker", "localhost")
        self.port = config.get("port", 1883)
        self.username = config.get("username", "")
        self.password = config.get("password", "")
        device_name = config.get("device_name", "ESPro_AI_Watch").replace(" ", "_")
        self.topic_base = f"espro/{device_name}/meeting"

        self._command_cb = command_callback
        self._client = None
        self.connected = False
        self.last_states = {}

    def connect(self):
        """Connect to MQTT broker."""
        if mqtt is None:
            log.error("paho-mqtt not installed")
            return

        self._client = mqtt.Client(
            client_id=f"espro_meeting_{uuid.uuid4().hex[:8]}", protocol=mqtt.MQTTv311
        )

        if self.username:
            self._client.username_pw_set(self.username, self.password)

        self._client.on_connect = self._on_connect
        self._client.on_message = self._on_message
        self._client.on_disconnect = self._on_disconnect

        try:
            self._client.connect(self.broker, self.port, keepalive=60)
            self._client.loop_start()
            log.info(f"Connecting to MQTT broker {self.broker}:{self.port}")
        except Exception as e:
            log.error(f"MQTT connect error: {e}")

    def disconnect(self):
        """Disconnect from broker."""
        if self._client:
            self._client.loop_stop()
            self._client.disconnect()
            self.connected = False

    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            self.connected = True
            log.info("MQTT connected")

            # Subscribe to command topics from watch
            cmd_topic = f"{self.topic_base}/cmd/#"
            client.subscribe(cmd_topic, 1)
            log.info(f"Subscribed to {cmd_topic}")
        else:
            log.error(f"MQTT connect failed, rc={rc}")

    def _on_disconnect(self, client, userdata, rc):
        self.connected = False
        if rc != 0:
            log.warning(f"MQTT unexpected disconnect, rc={rc}")

    def _on_message(self, client, userdata, msg):
        """Handle incoming messages from watch."""
        topic = msg.topic
        payload = msg.payload.decode("utf-8", errors="replace")

        prefix = f"{self.topic_base}/cmd/"
        if topic.startswith(prefix):
            cmd = topic[len(prefix) :]
            log.debug(f"Watch cmd: {cmd} = {payload}")
            if self._command_cb:
                self._command_cb(cmd, payload)

    def publish_state(self, key: str, value: str):
        """Publish meeting state to watch."""
        if not self._client or not self.connected:
            return
        topic = f"{self.topic_base}/state/{key}"
        self._client.publish(topic, value, qos=1, retain=True)
        self.last_states[key] = value

    def publish_notes(self, key: str, text: str):
        """Publish notes/summary to watch."""
        if not self._client or not self.connected:
            return
        topic = f"{self.topic_base}/notes/{key}"
        # Truncate for MQTT + watch display
        truncated = text[:250] if len(text) > 250 else text
        self._client.publish(topic, truncated, qos=1)
