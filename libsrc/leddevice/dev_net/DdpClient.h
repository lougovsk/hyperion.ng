#ifndef DDPCLIENT_H
#define DDPCLIENT_H

#include "UdpClient.h"
#include <vector>
#include <utils/ColorRgb.h> // For ColorRgb

// DDP header format and constants
namespace DDP {
	// DDP protocol header definitions
	struct Header {
		uint8_t flags1;
		uint8_t flags2;
		uint8_t type;
		uint8_t id;
		uint8_t offset[4]; // uint32_t
		uint8_t len[2];    // uint16_t
	};

	static constexpr int HEADER_LEN = (sizeof(struct Header));
	static constexpr int MAX_LEDS_PER_PACKET = 480; // Max LEDs for a single DDP packet (1440 bytes / 3 channels)
	static constexpr int CHANNELS_PER_PACKET = MAX_LEDS_PER_PACKET * 3;


	namespace flags1 {
		static constexpr auto VER_MASK = 0xc0;
		static constexpr auto VER1 = 0x40; // Protocol version 1
		static constexpr auto PUSH = 0x01;
		static constexpr auto QUERY = 0x02;
		static constexpr auto REPLY = 0x04;
		static constexpr auto STORAGE = 0x08;
		static constexpr auto TIME = 0x10; // Timestamp flag
	} // namespace flags1

	namespace id {
		static constexpr auto DISPLAY = 1; // Pixel data
		// Other IDs not strictly needed for sending pixel data but good for reference
		static constexpr auto CONTROL = 246;
		static constexpr auto CONFIG = 250;
		static constexpr auto STATUS = 251;
		static constexpr auto DMXTRANSIT = 254;
		static constexpr auto ALLDEVICES = 255;
	} // namespace id
} // namespace DDP

class DdpClient : public UdpClient
{
	Q_OBJECT

public:
	DdpClient(const QString& hostName, int port, QObject* parent = nullptr);

	// The ledCount parameter from LedDevice is _ledCount.
	// The _ledRGBCount parameter from LedDevice is _ledCount * 3.
	int sendDdpPacket(const std::vector<ColorRgb>& ledValues, int ledCount);

private:
	QByteArray _ddpBuffer;
	int _packageSequenceNumber;
};

#endif // DDPCLIENT_H
