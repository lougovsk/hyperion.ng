#ifndef LEDDEVICEWLED_H
#define LEDDEVICEWLED_H

// LedDevice includes
#include <leddevice/LedDevice.h> // Main base class
#include "DdpClient.h"           // For DDP streaming
#include "RawClient.h"           // For Raw UDP streaming
#include "WledRestClient.h"      // For WLED REST API communication

#include <utils/version.hpp>
#include <memory> // For std::unique_ptr

///
/// Implementation of a WLED-device
///
class LedDeviceWled : public LedDevice // Inherit from LedDevice directly
{

public:
	///
	/// @brief Constructs a WLED-device
	///
	/// @param deviceConfig Device's configuration as JSON-Object
	///
	explicit LedDeviceWled(const QJsonObject &deviceConfig);

	///
	/// @brief Destructor of the WLED-device
	///
	~LedDeviceWled() override;

	///
	/// @brief Constructs the WLED-device
	///
	/// @param[in] deviceConfig Device's configuration as JSON-Object
	/// @return LedDevice constructed
	static LedDevice* construct(const QJsonObject &deviceConfig);

	///
	/// @brief Discover WLED devices available (for configuration).
	///
	/// @param[in] params Parameters used to overwrite discovery default behaviour
	///
	/// @return A JSON structure holding a list of devices found
	///
	QJsonObject discover(const QJsonObject& params) override;

	///
	/// @brief Get the WLED device's resource properties
	///
	/// Following parameters are required
	/// @code
	/// {
	///     "host"  : "hostname or IP",
	///     "filter": "resource to query", root "/" is used, if empty
	/// }
	///@endcode
	///
	/// @param[in] params Parameters to query device
	/// @return A JSON structure holding the device's properties
	///
	QJsonObject getProperties(const QJsonObject& params) override;

	///
	/// @brief Send an update to the WLED device to identify it.
	///
	/// Following parameters are required
	/// @code
	/// {
	///     "host"  : "hostname or IP",
	/// }
	///@endcode
	///
	/// @param[in] params Parameters to address device
	///
	void identify(const QJsonObject& params) override;

protected:

	///
	/// @brief Initialise the WLED device's configuration and network address details
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
	/// @brief Closes the UDP device.
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

	///
	/// @brief Power-/turn on the WLED device.
	///
	/// @return True if success
	///
	bool powerOn() override;

	///
	/// @brief Power-/turn off the WLED device.
	///
	/// @return True if success
	///
	bool powerOff() override;

	///
	/// @brief Store the device's original state.
	///
	/// Save the device's state before hyperion color streaming starts allowing to restore state during switchOff().
	///
	/// @return True if success
	///
	bool storeState() override;

	///
	/// @brief Restore the device's original state.
	///
	/// Restore the device's state as before hyperion color streaming started.
	/// This includes the on/off state of the device.
	///
	/// @return True, if success
	///
	bool restoreState() override;

private:
	// Removed: openRestAPI, getUdpnObject, getSegmentObject, sendStateUpdateRequest
	// These are now part of WledRestClient or handled differently.

	bool isReadyForSegmentStreaming(const semver::version& version) const; // Made const, takes const ref
	bool isReadyForDDPStreaming(const semver::version& version) const;   // Made const, takes const ref

	// Removed: resolveAddress - NetUtils::resolveHostToAddress is used directly or within clients

	// Streaming clients
	std::unique_ptr<DdpClient> _ddpClient;
	std::unique_ptr<RawClient> _rawClient;

	// WLED REST API client
	std::unique_ptr<WledRestClient> _wledRestClient;

	// Configuration and State members
	QString _hostAddressResolved; // Store the resolved IP address
	QString _hostName;
	int _apiPort;
	int _streamPort; // Port for DDP or Raw UDP streaming

	QJsonObject _wledInfo; // Might be fetched and stored via _wledRestClient
	QJsonObject _originalStateProperties; // Might be fetched and stored via _wledRestClient

	semver::version _currentVersion; // Parsed from _wledInfo

	bool _isBrightnessOverwrite;
	int _brightness;

	bool _isSyncOverwrite;
	bool _originalStateUdpnSend; // From original state, used for restore
	bool _originalStateUdpnRecv; // From original state, used for restore

	bool _isStreamDDP; // True if DDP protocol is used, false for Raw UDP

	int _streamSegmentId;
	bool _isSwitchOffOtherSegments;
	bool _isStreamToSegment; // True if streaming to a specific segment
};

#endif // LEDDEVICEWLED_H
