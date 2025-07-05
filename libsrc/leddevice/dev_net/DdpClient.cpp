#include "DdpClient.h"
#include <QtEndian> // For qToBigEndian
#include <utils/Logger.h>

DdpClient::DdpClient(const QString& hostName, int port, QObject* parent)
	: UdpClient(hostName, port, parent)
	, _packageSequenceNumber(0)
{
	// Initial size, can be resized if needed, but DDP::CHANNELS_PER_PACKET is the max data payload
	_ddpBuffer.resize(DDP::HEADER_LEN + DDP::CHANNELS_PER_PACKET);
	// Set static header fields that don't change per packet (or per segment of a frame)
	_ddpBuffer[2] = 1; // type (always 1 for per-pixel data)
	_ddpBuffer[3] = DDP::id::DISPLAY; // id (always 1 for pixel data)
}

int DdpClient::sendDdpPacket(const std::vector<ColorRgb>& ledValues, int ledCount)
{
	if (!isDeviceReady() || isDeviceInError())
	{
		// Error logged by UdpClient or LedDevice
		return -1;
	}

	if (ledValues.empty() || ledCount == 0)
	{
		return 0; // Nothing to send
	}

	int rc = 0;
	const int totalChannels = ledCount * 3; // Total R,G,B channels to send
	int channelsSent = 0; // Number of channels sent so far

	// Prepare the DDP header view
	DDP::Header* header = reinterpret_cast<DDP::Header*>(_ddpBuffer.data());

	while (channelsSent < totalChannels)
	{
		if (_packageSequenceNumber > 15) // Sequence number is 4 bits
		{
			_packageSequenceNumber = 0;
		}

		int channelsInThisPacket = std::min(DDP::CHANNELS_PER_PACKET, totalChannels - channelsSent);

		// Set header flags1
		header->flags1 = DDP::flags1::VER1; // Version 1
		if ((channelsSent + channelsInThisPacket) >= totalChannels)
		{
			// This is the last packet (or only packet) for this frame, set PUSH flag
			header->flags1 |= DDP::flags1::PUSH;
		}

		// Set header flags2 (sequence number)
		// Bits 0-3 are sequence number. Bits 4-7 are reserved (0).
		header->flags2 = static_cast<uint8_t>(_packageSequenceNumber++ & 0x0F);

		// Set data offset (big endian)
		qToBigEndian<quint32>(static_cast<quint32>(channelsSent), header->offset);

		// Set data length (big endian) - number of data bytes in this packet
		qToBigEndian<quint16>(static_cast<quint16>(channelsInThisPacket), header->len);

		// Copy LED data into the buffer after the header
		// The ledValues data is already in RGBRGB... format.
		// We need to copy channelsInThisPacket bytes from the correct offset in ledValues.
		// channelsSent is the offset in terms of channels, so it's also the byte offset.
		memcpy(_ddpBuffer.data() + DDP::HEADER_LEN,
			   reinterpret_cast<const uint8_t*>(ledValues.data()) + channelsSent,
			   channelsInThisPacket);

		// Adjust QByteArray size to the actual packet size (header + data for this packet)
		_ddpBuffer.resize(DDP::HEADER_LEN + channelsInThisPacket);

		rc = writeBytes(_ddpBuffer);
		if (rc != 0)
		{
			// Error already logged by writeBytes
			break;
		}
		channelsSent += channelsInThisPacket;
	}
	return rc;
}
