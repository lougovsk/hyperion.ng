#ifndef RAWCLIENT_H
#define RAWCLIENT_H

#include "UdpClient.h"
#include <vector>
#include <utils/ColorRgb.h> // For ColorRgb

class RawClient : public UdpClient
{
	Q_OBJECT

public:
	RawClient(const QString& hostName, int port, QObject* parent = nullptr);

	// ledRGBCount is _ledCount * 3
	int sendRawPacket(const std::vector<ColorRgb>& ledValues, int ledRGBCount);
};

#endif // RAWCLIENT_H
