#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <cstring>

// =====================================================
// CONFIGURATION
// =====================================================

static constexpr uint8_t WIFI_CHANNEL = 1;
static constexpr uint8_t NOISE_PERCENT = 10;
static constexpr size_t MAX_MESSAGE_LENGTH = 48;

static constexpr uint32_t PACKET_MAGIC = 0x51524543;  // "QREC"
static constexpr uint8_t REPETITION_FACTOR = 3;

// Broadcast MAC: receiver does not need to be configured
// with the transmitter's MAC address.
uint8_t broadcastAddress[] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

// =====================================================
// PACKET FORMAT
//
// Both ESP32s MUST use this exact structure.
// Each character produces 24 encoded bits, packed
// into 3 bytes.
// =====================================================

struct TxPacket {
  uint32_t magic;
  uint32_t sequence;
  uint8_t noisePercent;
  uint8_t messageLength;
  uint16_t flipCount;
  char raw[MAX_MESSAGE_LENGTH];
  uint8_t noisyPacked[MAX_MESSAGE_LENGTH * 3];
};

static_assert(sizeof(TxPacket) <= ESP_NOW_MAX_DATA_LEN,
              "Packet exceeds ESP-NOW v1 payload limit");

uint32_t sequenceNumber = 0;

// =====================================================
// INJECT NOISE
// =====================================================

bool injectNoise(uint8_t &bit) {
  if ((esp_random() % 100) < NOISE_PERCENT) {
    bit ^= 1;
    return true;
  }

  return false;
}

// =====================================================
// PACK ONE BIT INTO THE OUTPUT BYTE ARRAY
// =====================================================

void packBit(uint8_t *buffer, size_t bitIndex, uint8_t bit) {
  if (bit) {
    buffer[bitIndex / 8] |=
      (uint8_t)(1U << (7 - (bitIndex % 8)));
  }
}

// =====================================================
// BUILD PACKET WITH REPETITION ENCODING + NOISE
// =====================================================

void buildPacket(const String &message, TxPacket &packet) {
  memset(&packet, 0, sizeof(packet));

  packet.magic = PACKET_MAGIC;
  packet.sequence = ++sequenceNumber;
  packet.noisePercent = NOISE_PERCENT;
  packet.messageLength = (uint8_t)message.length();

  memcpy(packet.raw, message.c_str(), message.length());

  size_t outputBitIndex = 0;

  for (size_t charIndex = 0;
       charIndex < message.length();
       charIndex++) {

    uint8_t character = (uint8_t)message[charIndex];

    // Process each character MSB first.
    for (int bitPosition = 7; bitPosition >= 0; bitPosition--) {
      uint8_t originalBit = (character >> bitPosition) & 1;

      // Repeat each source bit three times.
      for (uint8_t copyIndex = 0;
           copyIndex < REPETITION_FACTOR;
           copyIndex++) {

        uint8_t noisyBit = originalBit;

        if (injectNoise(noisyBit)) {
          packet.flipCount++;
        }

        packBit(packet.noisyPacked, outputBitIndex, noisyBit);
        outputBitIndex++;
      }
    }
  }
}

// =====================================================
// SETUP ESP-NOW
// =====================================================

void setupEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_wifi_set_channel(
        WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    Serial.println("ERROR: Could not set Wi-Fi channel.");
    while (true) delay(1000);
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW initialization failed.");
    while (true) delay(1000);
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastAddress, 6);
  peer.channel = WIFI_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("ERROR: Could not add broadcast peer.");
    while (true) delay(1000);
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("QRecall TX - ESP-NOW Noise Injection");
  Serial.println("------------------------------------");

  Serial.print("TX MAC: ");
  Serial.println(WiFi.macAddress());

  Serial.print("Noise probability: ");
  Serial.print(NOISE_PERCENT);
  Serial.println("%");

  Serial.print("Wi-Fi channel: ");
  Serial.println(WIFI_CHANNEL);

  setupEspNow();

  Serial.println("ESP-NOW ready.");
  Serial.println("Enter a message and press Enter.");
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {
  if (!Serial.available()) {
    delay(5);
    return;
  }

  String message = Serial.readStringUntil('\n');
  message.trim();

  if (message.length() == 0) {
    return;
  }

  if (message.length() > MAX_MESSAGE_LENGTH) {
    Serial.printf(
      "ERROR: Message exceeds %u characters.\n",
      (unsigned)MAX_MESSAGE_LENGTH
    );
    return;
  }

  TxPacket packet;
  buildPacket(message, packet);

  const size_t packetLength = sizeof(packet);

  Serial.println();
  Serial.println("----- TX DEBUG -----");

  Serial.print("Original: ");
  Serial.println(message);

  Serial.printf("Sequence: %lu\n",
                (unsigned long)packet.sequence);

  Serial.printf("Encoded bits: %u\n",
                (unsigned)(message.length() * 24));

  Serial.printf("Flipped bits: %u\n",
                packet.flipCount);

  Serial.printf("Configured noise: %u%%\n",
                packet.noisePercent);

  esp_err_t result = esp_now_send(
    broadcastAddress,
    reinterpret_cast<const uint8_t *>(&packet),
    packetLength
  );

  if (result == ESP_OK) {
    Serial.println("ESP-NOW send request accepted.");
  } else {
    Serial.printf("ESP-NOW send request failed: %d\n",
                  (int)result);
  }

  Serial.println("--------------------");

  // Allow the radio time between transmissions.
  delay(50);
}
