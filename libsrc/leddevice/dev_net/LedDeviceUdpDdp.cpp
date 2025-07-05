#include "LedDeviceUdpDdp.h"
#include "DdpClient.h" // Include the new client
#include <utils/NetUtils.h> // For DDP_DEFAULT_PORT, CONFIG_HOST, CONFIG_PORT, this might be better in a central place

// Constants
namespace {
	const char CONFIG_HOST[] = "host";
	const char CONFIG_PORT[] = "port";
	const ushort DDP_DEFAULT_PORT = 4048;
} //End of constants

LedDeviceUdpDdp::LedDeviceUdpDdp(const QJsonObject &deviceConfig)
	: LedDevice(deviceConfig)
	, _ddpClient(nullptr)
	, _port(DDP_DEFAULT_PORT)
{
}

LedDeviceUdpDdp::~LedDeviceUdpDdp()
{
	// _ddpClient is a unique_ptr, will be cleaned up automatically
}

LedDevice* LedDeviceUdpDdp::construct(const QJsonObject &deviceConfig)
{
	return new LedDeviceUdpDdp(deviceConfig);
}

bool LedDeviceUdpDdp::init(const QJsonObject &deviceConfig)
{
	// Call base class init first
	if ( !LedDevice::init(deviceConfig) )
	{
		return false;
	}

	_hostName = _devConfig[ CONFIG_HOST ].toString();
	_port = deviceConfig[CONFIG_PORT].toInt(DDP_DEFAULT_PORT);

	Debug(_log, "DDP Device Params: host %s, port %d", QSTRING_CSTR(_hostName), _port);

	_ddpClient = std::make_unique<DdpClient>(_hostName, _port);

	return true;
}

int LedDeviceUdpDdp::open()
{
	_isDeviceReady = false;
	if (!_ddpClient)
	{
		setInError("DDP Client not initialized.");
		return -1;
	}

	if (_ddpClient->open() == 0)
	{
		if (_ddpClient->isDeviceReady())
		{
			_isDeviceReady = true;
			return 0;
		}
		setInError(_ddpClient->getError().isEmpty() ? "DDP Client failed to become ready." : _ddpClient->getError());
		return -1;
	}

	setInError(_ddpClient->getError().isEmpty() ? "DDP Client failed to open." : _ddpClient->getError());
	return -1;
}

int LedDeviceUdpDdp::close()
{
	_isDeviceReady = false;
	if (_ddpClient)
	{
		// UdpClient::close always returns 0, consider if error state from client needs to be propagated
		return _ddpClient->close();
	}
	return 0;
}

int LedDeviceUdpDdp::write(const std::vector<ColorRgb> &ledValues)
{
	if (!_isDeviceReady || !_ddpClient)
	{
		// This case should ideally be caught by LedDevice::write prior to calling this
		// or _isDeviceReady should be false if client is null
		return -1;
	}

	int result = _ddpClient->sendDdpPacket(ledValues, static_cast<int>(_ledCount));
	if (result != 0 && _ddpClient->isDeviceInError())
	{
		setInError(_ddpClient->getError());
	}
	return result;
}
