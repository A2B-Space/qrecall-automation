#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <cstring>

// =====================================================
// CONFIGURATION
// Must match TXNoiseDebug.ino
// =====================================================

static constexpr uint8_t WIFI_CHANNEL = 1;
static constexpr size_t MAX_MESSAGE_LENGTH = 48;
static constexpr uint32_t PACKET_MAGIC = 0x51524543;

// =====================================================
// PACKET FORMAT
// Must match the transmitter exactly.
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

// =====================================================
// RECEIVE BUFFER
// The callback copies data; JSON output occurs in loop().
// =====================================================

TxPacket receivedPacket;
volatile bool packetReady = false;
char senderMac[18] = "unknown";
uint32_t receivedCount = 0;

// =====================================================
// FORMAT MAC ADDRESS
// =====================================================

void formatMac(const uint8_t *mac, char *output, size_t size) {
  snprintf(
    output,
    size,
    "%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
  );
}

// =====================================================
// ESP-NOW RECEIVE CALLBACK
//
// No decoding, correction, or serial printing here.
// =====================================================

void onDataReceived(
  const esp_now_recv_info_t *info,
  const uint8_t *data,
  int dataLength
) {
  if (dataLength != (int)sizeof(TxPacket)) {
    return;
  }

  if (packetReady) {
    // Do not overwrite a packet that has not yet been
    // processed by loop(). This packet is dropped.
    return;
  }

  TxPacket incoming;
  memcpy(&incoming, data, sizeof(incoming));

  if (incoming.magic != PACKET_MAGIC) {
    return;
  }

  if (incoming.messageLength > MAX_MESSAGE_LENGTH) {
    return;
  }

  memcpy(&receivedPacket, &incoming, sizeof(incoming));

  if (info != nullptr && info->src_addr != nullptr) {
    formatMac(info->src_addr, senderMac, sizeof(senderMac));
  } else {
    strcpy(senderMac, "unknown");
  }

  packetReady = true;
}

// =====================================================
// JSON STRING ESCAPING
// =====================================================

void printJsonString(const char *value, size_t length) {
  Serial.print('"');

  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)value[i];

    switch (c) {
      case '"':
        Serial.print("\\\"");
        break;

      case '\\':
        Serial.print("\\\\");
        break;

      case '\b':
        Serial.print("\\b");
        break;

      case '\f':
        Serial.print("\\f");
        break;

      case '\n':
        Serial.print("\\n");
        break;

      case '\r':
        Serial.print("\\r");
        break;

      case '\t':
        Serial.print("\\t");
        break;

      default:
        if (c < 0x20) {
          Serial.printf("\\u%04X", c);
        } else {
          Serial.write(c);
        }
        break;
    }
  }

  Serial.print('"');
}

// =====================================================
// PRINT ONE JSON RECORD
//
// noisy_encoded_hex stores the packed noisy bits.
// Every character corresponds to 3 bytes / 24 bits.
// =====================================================

void printJsonRecord(const TxPacket &packet, const char *mac) {
  const size_t encodedBytes = (size_t)packet.messageLength * 3;
  const size_t encodedBits = (size_t)packet.messageLength * 24;

  const float observedNoise = encodedBits > 0
    ? (100.0f * packet.flipCount / encodedBits)
    : 0.0f;

  Serial.print("{\"seq\":");
  Serial.print(packet.sequence);

  Serial.print(",\"raw\":");
  printJsonString(packet.raw, packet.messageLength);

  Serial.print(",\"noise_percent\":");
  Serial.print(packet.noisePercent);

  Serial.print(",\"flip_count\":");
  Serial.print(packet.flipCount);

  Serial.print(",\"encoded_bits_per_char\":24");

  Serial.print(",\"encoded_bits_total\":");
  Serial.print(encodedBits);

  Serial.print(",\"observed_noise_percent\":");
  Serial.print(observedNoise, 2);

  Serial.print(",\"noisy_encoded_hex\":\"");

  for (size_t i = 0; i < encodedBytes; i++) {
    Serial.printf("%02X", packet.noisyPacked[i]);
  }

  Serial.print('"');

  Serial.print(",\"sender_mac\":");
  printJsonString(mac, strlen(mac));

  Serial.print(",\"receiver_count\":");
  Serial.print(++receivedCount);

  Serial.println("}");
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

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

  if (esp_now_register_recv_cb(onDataReceived) != ESP_OK) {
    Serial.println("ERROR: Receive callback registration failed.");
    while (true) delay(1000);
  }

  // Setup text is not JSON. The PC logger will ignore
  // lines that are not valid JSON records.
  Serial.println("QRecall RX ready.");
  Serial.print("RX MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.printf("Listening on channel %u\n", WIFI_CHANNEL);
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {
  if (packetReady) {
    // Copy the packet while briefly preventing the callback
    // from replacing it.
    noInterrupts();

    TxPacket localPacket;
    memcpy(&localPacket, &receivedPacket, sizeof(localPacket));

    char localMac[18];
    memcpy(localMac, senderMac, sizeof(localMac));
    localMac[sizeof(localMac) - 1] = '\0';

    packetReady = false;

    interrupts();

    printJsonRecord(localPacket, localMac);
  }

  delay(1);
}
