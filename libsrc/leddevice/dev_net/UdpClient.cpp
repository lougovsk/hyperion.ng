#include "UdpClient.h"
#include <utils/NetUtils.h> // For NetUtils::resolveHostToAddress

UdpClient::UdpClient(const QString& hostName, int port, QObject* parent)
	: QObject(parent)
	, _log(Logger::getInstance("UDPCLIENT"))
	, _udpSocket(nullptr)
	, _hostName(hostName)
	, _port(port)
	, _isDeviceReady(false)
	, _isDeviceInError(false)
{
}

UdpClient::~UdpClient()
{
	delete _udpSocket;
}

void UdpClient::setInError(const QString& errorMsg)
{
	_isDeviceInError = true;
	_errorReason = errorMsg;
	Error(_log, "%s", QSTRING_CSTR(errorMsg));
}

int UdpClient::open()
{
	int retval = -1;
	_isDeviceReady = false;
	_isDeviceInError = false;
	_errorReason.clear();

	if (_udpSocket == nullptr)
	{
		_udpSocket = new QUdpSocket(this);
	}

	if (NetUtils::resolveHostToAddress(_log, _hostName, _address))
	{
		// Try to bind the UDP-Socket
		if (_udpSocket != nullptr)
		{
			Info(_log, "Stream UDP data to %s port: %d", QSTRING_CSTR(_address.toString()), _port);
			if (_udpSocket->state() != QAbstractSocket::BoundState)
			{
				QHostAddress localAddress = QHostAddress::Any;
				quint16 localPort = 0;
				if (!_udpSocket->bind(localAddress, localPort))
				{
					QString warntext = QString("Could not bind local address: %1, (%2) %3").arg(localAddress.toString()).arg(_udpSocket->error()).arg(_udpSocket->errorString());
					Warning(_log, "%s", QSTRING_CSTR(warntext));
					// Optionally set in error, though original ProviderUdp didn't explicitly for bind failure of local
				}
			}
			_isDeviceReady = true; // If resolve and socket init is ok, consider ready to write
			retval = 0;
		}
		else
		{
			setInError("Open error. UDP Socket not initialised!");
		}
	}
	else
	{
		// error resolving hostname is already logged by NetUtils::resolveHostToAddress
		// setInError is not called here in ProviderUdp, LedDevice::open handles it
		// For UdpClient, we might want to set it explicitly if resolve fails
		setInError(QString("Failed to resolve hostname: %1").arg(_hostName));
	}
	return retval;
}

int UdpClient::close()
{
	int retval = 0;
	_isDeviceReady = false;

	if (_udpSocket != nullptr)
	{
		if (_udpSocket->isOpen()) // Check if socket is open before closing
		{
			Debug(_log, "Close UDP-client: %s:%d", QSTRING_CSTR(_address.toString()), _port);
			_udpSocket->close();
		}
	}
	return retval;
}

int UdpClient::writeBytes(const unsigned size, const uint8_t* data)
{
	if (!_isDeviceReady || _isDeviceInError)
	{
		//Debug(_log, "Device not ready or in error state. Skipping write.");
		return -1;
	}

	qint64 bytesWritten = _udpSocket->writeDatagram(reinterpret_cast<const char*>(data), size, _address, static_cast<quint16>(_port));

	if (bytesWritten == -1 || bytesWritten != static_cast<qint64>(size))
	{
		Warning(_log, "%s", QSTRING_CSTR(QString("(%1:%2) Write Error: (%3) %4").arg(_address.toString()).arg(_port).arg(_udpSocket->error()).arg(_udpSocket->errorString())));
		return -1;
	}
	return 0;
}

int UdpClient::writeBytes(const QByteArray& bytes)
{
	if (!_isDeviceReady || _isDeviceInError)
	{
		//Debug(_log, "Device not ready or in error state. Skipping write.");
		return -1;
	}

	qint64 bytesWritten = _udpSocket->writeDatagram(bytes, _address, static_cast<quint16>(_port));

	if (bytesWritten == -1 || bytesWritten != bytes.size())
	{
		Warning(_log, "%s", QSTRING_CSTR(QString("(%1:%2) Write Error: (%3) %4").arg(_address.toString()).arg(_port).arg(_udpSocket->error()).arg(_udpSocket->errorString())));
		return -1;
	}
	return 0;
}
