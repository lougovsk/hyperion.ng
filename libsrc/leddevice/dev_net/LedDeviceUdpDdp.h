#ifndef LEDEVICEUDPDDP_H
#define LEDEVICEUDPDDP_H

// LedDevice includes
#include <leddevice/LedDevice.h>

// Forward declaration
class DdpClient;

///
/// Implementation of the LedDevice interface for sending LED colors via UDP and the Distributed Display Protocol (DDP)
/// http://www.3waylabs.com/ddp/#Data%20Types
///
class LedDeviceUdpDdp : public LedDevice
{
public:

	///
	/// @brief Constructs a LED-device fed via DDP
	///
	/// @param deviceConfig Device's configuration as JSON-Object
	///
	explicit LedDeviceUdpDdp(const QJsonObject &deviceConfig);

	///
	/// @brief Destructor of the LedDevice
	///
	~LedDeviceUdpDdp() override;

	///
	/// @brief Constructs the LED-device
	///
	/// @param[in] deviceConfig Device's configuration as JSON-Object
	/// @return LedDevice constructed
	///
	static LedDevice* construct(const QJsonObject &deviceConfig);

protected:

	///
	/// @brief Initialise the device's configuration
	///
	/// @param[in] deviceConfig the JSON device configuration
	/// @return True, if success
	///
	bool init(const QJsonObject &deviceConfig) override;

	///
	/// @brief Opens the output device.
	///
	/// @return Zero on success (i.e. device is ready), else negative
	///
	int open() override;

	///
	/// @brief Closes the output device.
	///
	/// @return Zero on success (i.e. device is closed), else negative
	///
	int close() override;

	///
	/// @brief Writes the RGB-Color values to the LEDs.
	///
	/// @param[in] ledValues The RGB-color per LED
	/// @return Zero on success, else negative
	///
	int write(const std::vector<ColorRgb> & ledValues) override;

private:
	std::unique_ptr<DdpClient> _ddpClient;
	QString _hostName;
	int _port;
};

#endif // LEDEVICEUDPDDP_H
