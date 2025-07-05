#include "RawClient.h"
#include <utils/Logger.h>

RawClient::RawClient(const QString& hostName, int port, QObject* parent)
	: UdpClient(hostName, port, parent)
{
}

int RawClient::sendRawPacket(const std::vector<ColorRgb>& ledValues, int ledRGBCount)
{
	if (!isDeviceReady() || isDeviceInError())
	{
		// Error logged by UdpClient or LedDevice
		return -1;
	}

	if (ledValues.empty() || ledRGBCount == 0)
	{
		return 0; // Nothing to send
	}

	// For Raw UDP, the data is just the byte sequence of colors.
	// ledValues.data() points to the beginning of this sequence.
	// ledRGBCount is the total number of bytes (R,G,B,R,G,B...).
	const uint8_t* dataPtr = reinterpret_cast<const uint8_t*>(ledValues.data());

	return writeBytes(static_cast<unsigned>(ledRGBCount), dataPtr);
}
