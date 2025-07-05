#ifndef UDPCLIENT_H
#define UDPCLIENT_H

#include <QObject> // Required for Q_OBJECT
#include <QHostAddress>
#include <QUdpSocket>
#include <QString>
#include <utils/Logger.h> // For Logger and _log

// Forward declaration
class Logger;

class UdpClient : public QObject
{
	Q_OBJECT

public:
	UdpClient(const QString& hostName, int port, QObject* parent = nullptr);
	~UdpClient() override;

	int open();
	int close();
	int writeBytes(const unsigned size, const uint8_t* data);
	int writeBytes(const QByteArray& bytes);
	bool isDeviceReady() const { return _isDeviceReady; }
	void setInError(const QString& errorMsg);
	QString getError() const { return _errorReason; }
	bool isDeviceInError() const { return _isDeviceInError; }

protected:
	Logger* _log;
	QUdpSocket* _udpSocket;
	QString _hostName;
	QHostAddress _address;
	int _port;
	bool _isDeviceReady;
	bool _isDeviceInError;
	QString _errorReason;
};

#endif // UDPCLIENT_H
