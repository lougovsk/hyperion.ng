#include "LedDeviceUdpRaw.h"
#include "RawClient.h" // Include the new client
#include <utils/NetUtils.h> // For RAW_DEFAULT_PORT, CONFIG_HOST, CONFIG_PORT

// Constants
namespace {
	const bool verbose = false; // Keep for getProperties if used elsewhere, or remove if only for local debug
	const char CONFIG_HOST[] = "host";
	const char CONFIG_PORT[] = "port";
	const ushort RAW_DEFAULT_PORT = 5568;
	const int UDP_MAX_LED_NUM = 490; // Max LEDs for a single UDP packet (1470bytes / 3 channels)
} //End of constants

LedDeviceUdpRaw::LedDeviceUdpRaw(const QJsonObject &deviceConfig)
	: LedDevice(deviceConfig)
	, _rawClient(nullptr)
	, _port(RAW_DEFAULT_PORT)
{
}

LedDeviceUdpRaw::~LedDeviceUdpRaw()
{
	// _rawClient is a unique_ptr, will be cleaned up automatically
}

LedDevice* LedDeviceUdpRaw::construct(const QJsonObject &deviceConfig)
{
	return new LedDeviceUdpRaw(deviceConfig);
}

bool LedDeviceUdpRaw::init(const QJsonObject &deviceConfig)
{
	if ( !LedDevice::init(deviceConfig) )
	{
		return false;
	}

	if (this->getLedCount() > UDP_MAX_LED_NUM)
	{
		QString errorReason = QString("Device type %1 can only be run with maximum %2 LEDs for streaming protocol = UDP-RAW!").arg(this->getActiveDeviceType()).arg(UDP_MAX_LED_NUM);
		this->setInError ( errorReason );
		return false;
	}

	_hostName = _devConfig[ CONFIG_HOST ].toString();
	_port = deviceConfig[CONFIG_PORT].toInt(RAW_DEFAULT_PORT);

	Debug(_log, "Raw UDP Device Params: host %s, port %d", QSTRING_CSTR(_hostName), _port);

	_rawClient = std::make_unique<RawClient>(_hostName, _port);

	return true;
}

int LedDeviceUdpRaw::open()
{
	_isDeviceReady = false;
	if (!_rawClient)
	{
		setInError("Raw UDP Client not initialized.");
		return -1;
	}

	if (_rawClient->open() == 0)
	{
		if(_rawClient->isDeviceReady())
		{
			_isDeviceReady = true;
			return 0;
		}
		setInError(_rawClient->getError().isEmpty() ? "Raw UDP Client failed to become ready." : _rawClient->getError());
		return -1;
	}
	setInError(_rawClient->getError().isEmpty() ? "Raw UDP Client failed to open." : _rawClient->getError());
	return -1;
}

int LedDeviceUdpRaw::close()
{
	_isDeviceReady = false;
	if (_rawClient)
	{
		return _rawClient->close();
	}
	return 0;
}

int LedDeviceUdpRaw::write(const std::vector<ColorRgb> &ledValues)
{
	if (!_isDeviceReady || !_rawClient)
	{
		return -1;
	}

	// _ledRGBCount is available from LedDevice base class
	int result = _rawClient->sendRawPacket(ledValues, _ledRGBCount);
	if (result != 0 && _rawClient->isDeviceInError())
	{
		setInError(_rawClient->getError());
	}
	return result;
}

QJsonObject LedDeviceUdpRaw::getProperties(const QJsonObject& params)
{
	DebugIf(verbose, _log, "params: [%s]", QString(QJsonDocument(params).toJson(QJsonDocument::Compact)).toUtf8().constData() );

	QJsonObject properties;
	// Call base class implementation for common properties
	// properties = LedDevice::getProperties(params);

	Info(_log, "Get properties for %s", QSTRING_CSTR(_activeDeviceType));

	QJsonObject propertiesDetails;
	propertiesDetails.insert("maxLedCount", UDP_MAX_LED_NUM);
	// Add any other Raw UDP specific properties here if needed

	properties.insert("properties", propertiesDetails);

	DebugIf(verbose, _log, "properties: [%s]", QString(QJsonDocument(properties).toJson(QJsonDocument::Compact)).toUtf8().constData() );

	return properties;
}
