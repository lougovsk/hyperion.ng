// Local-Hyperion includes
#include "LedDeviceWled.h"

// #include <chrono> // DEFAULT_IDENTIFY_TIME moved to WledRestClient
#include <utils/QStringUtils.h>
// #include <utils/WaitTime.h> // wait() is called by WledRestClient::identifyWled if needed.
#include <utils/JsonUtils.h> // For QJsonDocument in debug/logging

// mDNS discover
#ifdef ENABLE_MDNS
#include <mdns/MdnsBrowser.h>
#include <mdns/MdnsServiceRegister.h>
#endif
#include <utils/NetUtils.h> // For resolveHostToAddress and default ports
#include <utils/version.hpp>

// Constants moved to WledRestClient.h or defined locally if specific to LedDeviceWled
namespace {

const bool verbose = false; // For DebugIf

// Configuration settings - Keys
const char CONFIG_HOST[] = "host";
const char CONFIG_STREAM_PROTOCOL[] = "streamProtocol"; // "DDP" or "UDP-RAW"
const char CONFIG_RESTORE_STATE[] = "restoreOriginalState";
const char CONFIG_STAY_ON_AFTER_STREAMING[] = "stayOnAfterStreaming";

const char CONFIG_BRIGHTNESS[] = "brightness";
const char CONFIG_BRIGHTNESS_OVERWRITE[] = "overwriteBrightness";
const char CONFIG_SYNC_OVERWRITE[] = "overwriteSync"; // WLED specific sync (udpn.send/recv)

const char CONFIG_STREAM_SEGMENTS[] = "segments"; // JSON object for segment config
const char CONFIG_STREAM_SEGMENT_ID[] = "streamSegmentId";
const char CONFIG_SWITCH_OFF_OTHER_SEGMENTS[] = "switchOffOtherSegments";

// Default values for configuration
const char DEFAULT_STREAM_PROTOCOL[] = "DDP";
const int UDP_RAW_STREAM_DEFAULT_PORT = 19446; // Default port for WLED Raw UDP
const int DDP_STREAM_DEFAULT_PORT = 4048;     // Default port for DDP
const int UDP_RAW_MAX_LED_NUM = 490;          // Max LEDs for UDP Raw on WLED (WLED specific, not general UDP Raw limit)

// Version constraints for features
const char WLED_VERSION_DDP_SUPPORT[] = "0.11.0";
const char WLED_VERSION_SEGMENT_STREAMING_SUPPORT[] = "0.13.3";


// Default state values for WLED interaction logic
const bool DEFAULT_IS_RESTORE_STATE = false;
const bool DEFAULT_IS_STAY_ON_AFTER_STREAMING = false;
const bool DEFAULT_IS_BRIGHTNESS_OVERWRITE = true;
const int BRI_MAX = 255; // Max brightness value for WLED API
const bool DEFAULT_IS_SYNC_OVERWRITE = true;
const int DEFAULT_SEGMENT_ID = -1; // Represents no specific segment targeted, or main segment
const bool DEFAULT_IS_SWITCH_OFF_OTHER_SEGMENTS = true;

} //End of constants namespace

LedDeviceWled::LedDeviceWled(const QJsonObject &deviceConfig)
	: LedDevice(deviceConfig) // Initialize base class
	  // Pointers are initialized to nullptr by default or in class definition
	  ,_apiPort(WledConstants::API_DEFAULT_PORT) // Use constant from WledRestClient for API port
	  ,_streamPort(0) // Will be set in init()
	  ,_currentVersion("") // Initialize semver version string
	  ,_isBrightnessOverwrite(DEFAULT_IS_BRIGHTNESS_OVERWRITE)
	  ,_brightness (BRI_MAX)
	  ,_isSyncOverwrite(DEFAULT_IS_SYNC_OVERWRITE)
	  ,_originalStateUdpnSend(false)
	  ,_originalStateUdpnRecv(true) // Default WLED behavior is to receive
	  ,_isStreamDDP(true) // Default to DDP
	  ,_streamSegmentId(DEFAULT_SEGMENT_ID)
	  ,_isSwitchOffOtherSegments(DEFAULT_IS_SWITCH_OFF_OTHER_SEGMENTS)
	  ,_isStreamToSegment(false)
{
#ifdef ENABLE_MDNS
	// Consider if mDNS browsing should be initiated by WledRestClient or a more general discovery manager
	QMetaObject::invokeMethod(MdnsBrowser::getInstance().data(), "browseForServiceType",
							   Qt::QueuedConnection, Q_ARG(QByteArray, MdnsServiceRegister::getServiceType(_activeDeviceType)));
#endif
	// _wledRestClient, _ddpClient, _rawClient are std::unique_ptr, default initialized to nullptr
}

LedDeviceWled::~LedDeviceWled()
{
	// std::unique_ptr members (_wledRestClient, _ddpClient, _rawClient) are automatically cleaned up.
}

LedDevice* LedDeviceWled::construct(const QJsonObject &deviceConfig)
{
	return new LedDeviceWled(deviceConfig);
}

bool LedDeviceWled::init(const QJsonObject &deviceConfig)
{
	// Call base class init first
	if (!LedDevice::init(deviceConfig))
	{
		return false;
	}

	// Get hostname from config (common for REST and streaming)
	_hostName = _devConfig[CONFIG_HOST].toString();
	if (_hostName.isEmpty()) {
		this->setInError("WLED init failed: Hostname/IP address is empty.");
		return false;
	}

	// Determine streaming protocol
	QString streamProtocol = _devConfig[CONFIG_STREAM_PROTOCOL].toString(DEFAULT_STREAM_PROTOCOL).toUpper();
	_isStreamDDP = (streamProtocol == DEFAULT_STREAM_PROTOCOL);

	Debug(_log, "WLED Configuration:");
	Debug(_log, "  Hostname: %s", QSTRING_CSTR(_hostName));
	Debug(_log, "  Stream Protocol: %s", _isStreamDDP ? "DDP" : "UDP-RAW");

	// Initialize streaming client (DDP or RawUDP)
	if (_isStreamDDP)
	{
		_streamPort = _devConfig["port"].toInt(DDP_STREAM_DEFAULT_PORT); // DDP port from config or default
		_ddpClient = std::make_unique<DdpClient>(_hostName, _streamPort);
		_ddpClient->setLogger(_log); // Pass logger
		Debug(_log, "  DDP Streaming Port: %d", _streamPort);
	}
	else // UDP-RAW
	{
		_streamPort = _devConfig["port"].toInt(UDP_RAW_STREAM_DEFAULT_PORT); // Raw UDP port from config or default
		if (this->getLedCount() > UDP_RAW_MAX_LED_NUM)
		{
			QString errorReason = QString("Device type %1 with UDP-RAW protocol can only support up to %2 LEDs. Configured LEDs: %3.")
									  .arg(this->getActiveDeviceType())
									  .arg(UDP_RAW_MAX_LED_NUM)
									  .arg(this->getLedCount());
			this->setInError(errorReason);
			return false;
		}
		_rawClient = std::make_unique<RawClient>(_hostName, _streamPort);
		_rawClient->setLogger(_log); // Pass logger
		Debug(_log, "  Raw UDP Streaming Port: %d", _streamPort);
	}

	// Initialize WLED specific configurations
	_apiPort = WledConstants::API_DEFAULT_PORT; // Default, actual port might be resolved by NetUtils if not specified with host
	_isRestoreOrigState = _devConfig[CONFIG_RESTORE_STATE].toBool(DEFAULT_IS_RESTORE_STATE);
	_isStayOnAfterStreaming = _devConfig[CONFIG_STAY_ON_AFTER_STREAMING].toBool(DEFAULT_IS_STAY_ON_AFTER_STREAMING);
	_isSyncOverwrite = _devConfig[CONFIG_SYNC_OVERWRITE].toBool(DEFAULT_IS_SYNC_OVERWRITE);
	_isBrightnessOverwrite = _devConfig[CONFIG_BRIGHTNESS_OVERWRITE].toBool(DEFAULT_IS_BRIGHTNESS_OVERWRITE);
	_brightness = _devConfig[CONFIG_BRIGHTNESS].toInt(BRI_MAX);
	// Ensure brightness is within WLED limits (0-255)
	_brightness = std::max(0, std::min(_brightness, BRI_MAX));


	Debug(_log, "  Restore Original State: %s", _isRestoreOrigState ? "Yes" : "No");
	Debug(_log, "  Stay On After Streaming: %s", _isStayOnAfterStreaming ? "Yes" : "No");
	Debug(_log, "  Overwrite Sync Settings: %s", _isSyncOverwrite ? "Yes" : "No");
	Debug(_log, "  Overwrite Brightness: %s", _isBrightnessOverwrite ? "Yes" : "No");
	if (_isBrightnessOverwrite) {
		Debug(_log, "  Set Brightness to: %d", _brightness);
	}

	// Segment configuration
	QJsonObject segmentsConfig = _devConfig[CONFIG_STREAM_SEGMENTS].toObject();
	_streamSegmentId = segmentsConfig[CONFIG_STREAM_SEGMENT_ID].toInt(DEFAULT_SEGMENT_ID);
	_isStreamToSegment = (_streamSegmentId > DEFAULT_SEGMENT_ID); // Typically 0 is the first segment
	_isSwitchOffOtherSegments = segmentsConfig[CONFIG_SWITCH_OFF_OTHER_SEGMENTS].toBool(DEFAULT_IS_SWITCH_OFF_OTHER_SEGMENTS);

	Debug(_log, "  Stream to Specific Segment: %s", _isStreamToSegment ? "Yes" : "No");
	if (_isStreamToSegment)
	{
		Debug(_log, "    Target Segment ID: %d", _streamSegmentId);
		Debug(_log, "    Switch Off Other Segments: %s", _isSwitchOffOtherSegments ? "Yes" : "No");
	}

	// Note: _wledRestClient is initialized in open() after host resolution.
	// _address (QHostAddress) is now managed by individual clients or resolved as needed.
	// _hostAddressResolved will store the resolved IP for the REST client.

	return true; // Successfully initialized configurations
}


// bool LedDeviceWled::openRestAPI() // This method is now part of WledRestClient or handled by its constructor/open
// {
// 	// ...
// }

int LedDeviceWled::open()
{
	_isDeviceReady = false;
	int retval = -1;

	// Resolve hostname to IP address for REST API client
	// The streaming clients (DdpClient, RawClient) handle their own resolution internally if needed,
	// but we need the resolved address for WledRestClient.
	// _apiPort is initialized with WledConstants::API_DEFAULT_PORT, NetUtils will use default HTTP/S ports if not part of _hostName.
	QHostAddress resolvedAddress;
	if (!NetUtils::resolveHostToAddress(_log, _hostName, resolvedAddress, _apiPort))
	{
		// Error is logged by NetUtils
		this->setInError(QString("WLED Open failed: Could not resolve hostname '%1'.").arg(_hostName));
		return -1;
	}
	_hostAddressResolved = resolvedAddress.toString(); // Store resolved IP

	// Initialize WLED Rest Client (needs resolved address)
	// _apiPort might have been updated by resolveHostToAddress if it was specified like "host:port"
	_wledRestClient = std::make_unique<WledRestClient>(_hostAddressResolved, _apiPort, _log);
	if (!_wledRestClient->open()) {
		this->setInError(QString("WLED Open failed: Could not connect to WLED API at %1:%2. Error: %3")
							 .arg(_hostAddressResolved).arg(_apiPort).arg(_wledRestClient->getErrorReason()));
		return -1;
	}
	Debug(_log, "WLED REST API client opened successfully for %s:%d.", QSTRING_CSTR(_hostAddressResolved), _apiPort);


	// Open the appropriate streaming client
	if (_isStreamDDP)
	{
		if (_ddpClient && _ddpClient->open() == 0)
		{
			if (_ddpClient->isDeviceReady())
			{
				Info(_log, "WLED DDP streaming client opened successfully for %s:%d.", QSTRING_CSTR(_hostName), _streamPort);
				_isDeviceReady = true; // Mark device as ready for streaming
				retval = 0;
			}
			else
			{
				this->setInError(QString("WLED DDP client failed to become ready for %1:%2. Error: %3")
									 .arg(_hostName).arg(_streamPort)
									 .arg(_ddpClient->getError().isEmpty() ? "Unknown error" : _ddpClient->getError()));
			}
		}
		else
		{
			this->setInError(QString("WLED Open failed: Could not open DDP client for %1:%2. Error: %3")
								 .arg(_hostName).arg(_streamPort)
								 .arg(_ddpClient ? (_ddpClient->getError().isEmpty() ? "Unknown error" : _ddpClient->getError()) : "DDP client not initialized"));
		}
	}
	else // UDP-RAW
	{
		if (_rawClient && _rawClient->open() == 0)
		{
			if (_rawClient->isDeviceReady())
			{
				Info(_log, "WLED Raw UDP streaming client opened successfully for %s:%d.", QSTRING_CSTR(_hostName), _streamPort);
				_isDeviceReady = true; // Mark device as ready for streaming
				retval = 0;
			}
			else
			{
				this->setInError(QString("WLED Raw UDP client failed to become ready for %1:%2. Error: %3")
									 .arg(_hostName).arg(_streamPort)
									 .arg(_rawClient->getError().isEmpty() ? "Unknown error" : _rawClient->getError()));
			}
		}
		else
		{
			this->setInError(QString("WLED Open failed: Could not open Raw UDP client for %1:%2. Error: %3")
								 .arg(_hostName).arg(_streamPort)
								 .arg(_rawClient ? (_rawClient->getError().isEmpty() ? "Unknown error" : _rawClient->getError()) : "Raw client not initialized"));
		}
	}

	if (retval == 0)
	{
		// Optionally, after both REST and streaming clients are open,
		// fetch initial state/info here if needed immediately.
		// storeState() will likely be called by Hyperion core if device is enabled.
	}

	return retval;
}

int LedDeviceWled::close()
{
	_isDeviceReady = false; // Mark as not ready immediately
	int streamCloseRet = 0;

	// Close streaming client
	if (_isStreamDDP && _ddpClient)
	{
		Debug(_log, "Closing WLED DDP streaming client.");
		streamCloseRet = _ddpClient->close();
	}
	else if (!_isStreamDDP && _rawClient)
	{
		Debug(_log, "Closing WLED Raw UDP streaming client.");
		streamCloseRet = _rawClient->close();
	}

	// WledRestClient does not have an explicit close() for the REST API connection,
	// as ProviderRestApi manages connections per request.
	// We can reset the unique_ptr if we want to tear it down completely.
	if (_wledRestClient)
	{
		Debug(_log, "WLED REST client is being reset.");
		_wledRestClient.reset();
	}

	if (streamCloseRet != 0)
	{
		Warning(_log, "Error closing WLED streaming client.");
		// Depending on client implementation, error might already be set.
		// For now, we just log a warning.
	}

	return streamCloseRet; // Return status of streaming client close
}

// QJsonObject LedDeviceWled::getUdpnObject(bool isSendOn, bool isRecvOn) const // Moved to WledRestClient
// {
// 	// ...
// }

// QJsonObject LedDeviceWled::getSegmentObject(int segmentId, bool isOn, int brightness) const // Moved to WledRestClient
// {
// 	// ...
// }

// bool LedDeviceWled::sendStateUpdateRequest(const QJsonObject &request, const QString requestType) // Moved to WledRestClient
// {
// 	// ...
// }

bool LedDeviceWled::isReadyForSegmentStreaming(const semver::version& version) const
{
	bool isReady{false};
	if (version.isValid())
	{
		semver::version segmentStreamingVersion{WLED_VERSION_SEGMENT_STREAMING_SUPPORT};
		if (version < segmentStreamingVersion)
		{
			Warning(_log, "Segment streaming may not be fully supported by your WLED device version [%s]. Minimum recommended version is [%s].", version.getVersion().c_str(), segmentStreamingVersion.getVersion().c_str());
		}
		else
		{
			Debug(_log, "Segment streaming is supported by WLED device version [%s].", version.getVersion().c_str());
			isReady = true;
		}
	}
	else
	{
		Error(_log, "Provided WLED version is not valid, cannot check for segment streaming readiness.");
	}
	return isReady;
}

bool LedDeviceWled::isReadyForDDPStreaming(const semver::version& version) const
{
	bool isReady{false};
	if (version.isValid())
	{
		semver::version ddpVersion{WLED_VERSION_DDP_SUPPORT};
		if (version < ddpVersion)
		{
			Warning(_log, "DDP streaming may not be fully supported by your WLED device version [%s]. Minimum recommended version is [%s]. Consider using UDP-RAW if issues occur or ensure WLED firmware is updated.", version.getVersion().c_str(), ddpVersion.getVersion().c_str());
            // Note: The original code mentioned fallback to UDP-Raw with 490 LED limit.
            // This logic is now handled by the initial choice of _isStreamDDP.
            // If DDP is chosen and version is too low, it's a warning, not an automatic switch here.
		}
		else
		{
			Debug(_log, "DDP streaming is supported by WLED device version [%s].", version.getVersion().c_str());
			isReady = true;
		}
	}
	else
	{
		Error(_log, "Provided WLED version is not valid, cannot check for DDP streaming readiness.");
	}
	return isReady;
}

bool LedDeviceWled::powerOn()
{
	if (!_wledRestClient || !_wledRestClient->isDeviceReady())
	{
		Debug(_log, "WLED Rest client not ready, cannot power on.");
		// setInError might be too aggressive here if it's a transient issue,
		// but if open() failed, _wledRestClient wouldn't be ready.
		// Caller (Hyperion) should handle device not being ready.
		return false;
	}

	// Ensure we have the latest state/info if needed for powerOn logic, especially segment info
	// storeState() should have been called if _isRestoreOrigState or other flags are true.
	// If not, _originalStateProperties and _wledInfo might be stale or empty.
	// For safety, let's ensure they are populated if critical checks depend on them.
	// However, powerOn itself might not need to re-fetch if storeState did its job.
	// The WledRestClient::powerOnWled now expects these as parameters.

	bool success = _wledRestClient->powerOnWled(
		_isStreamToSegment,
		_originalStateProperties, // Must be valid if _isStreamToSegment is true
		_wledInfo,              // Must be valid if _isStreamToSegment is true (for liveseg check, version check)
		_streamSegmentId,
		_isSwitchOffOtherSegments,
		static_cast<int>(_ledCount),
		_isBrightnessOverwrite,
		_brightness,
		_isSyncOverwrite
	);

	if (!success) {
		this->setInError(QString("WLED powerOn failed: %1").arg(_wledRestClient->getErrorReason()));
	} else {
		// Check segment and DDP readiness based on the version obtained (likely in storeState)
		// These checks are important before actual streaming starts.
		// If storeState hasn't run (e.g. device just enabled), _currentVersion might be invalid.
		if (_isStreamToSegment && !isReadyForSegmentStreaming(_currentVersion))
		{
			// Error already logged by isReadyForSegmentStreaming if version is invalid or too low
			// WledRestClient might also have its own checks or rely on these.
			// For now, this seems like a good place for a high-level check in LedDeviceWled.
			this->setInError(QString("WLED version %1 does not support segment streaming (required %2).")
				.arg(QString::fromStdString(_currentVersion.getVersion()))
				.arg(WLED_VERSION_SEGMENT_STREAMING_SUPPORT));
			return false;
		}
		if (_isStreamDDP && !isReadyForDDPStreaming(_currentVersion))
		{
			this->setInError(QString("WLED version %1 does not support DDP streaming (required %2). UDP-RAW might be a fallback if configured.")
				.arg(QString::fromStdString(_currentVersion.getVersion()))
				.arg(WLED_VERSION_DDP_SUPPORT));
			// Note: actual fallback to RAW is based on initial config, not dynamically here.
			// If DDP was configured but not supported, it's an error state for DDP mode.
			return false;
		}
		// Specific check for "Use main segment only" if streaming to segments
		if (_isStreamToSegment && _wledInfo.contains(WledConstants::INFO_LIVESEG) && _wledInfo[WledConstants::INFO_LIVESEG].toInt(-1) == -1)
		{
			// This check was originally in powerOn, makes sense to keep it here after calling client.
			// Or WledRestClient could return a specific error for this.
			stopEnableAttemptsTimer(); // Ensure this method is available or handled by Hyperion core
			this->setInError( "Segment streaming configured, but \"Use main segment only\" in WLED Sync Interface configuration is not enabled!", false);
			return false;
		}

	}
	return success;
}

bool LedDeviceWled::powerOff()
{
	if (!_wledRestClient || !_wledRestClient->isDeviceReady())
	{
		Debug(_log, "WLED Rest client not ready, cannot power off.");
		return false; // Similar to powerOn, Hyperion should handle.
	}

	// Write a final "Black" to have a defined outcome before API call
	// This assumes the streaming client is still open and ready.
	if (_isDeviceReady) // _isDeviceReady refers to streaming readiness
	{
		writeBlack(); // writeBlack is a LedDevice method
	}

	bool success = _wledRestClient->powerOffWled(
		_isStreamToSegment,
		_streamSegmentId,
		_isStayOnAfterStreaming,
		_isSyncOverwrite,
		_originalStateUdpnSend,
		_originalStateUdpnRecv
	);

	if (!success) {
		// Don't set device in error for powerOff failure, but log it.
		Warning(_log, "WLED powerOff command failed: %1", QSTRING_CSTR(_wledRestClient->getErrorReason()));
	}
	return success; // Return actual success of API call.
}

bool LedDeviceWled::storeState()
{
	if (!_wledRestClient || !_wledRestClient->isDeviceReady())
	{
		// If called during enable, and REST client isn't ready (e.g. open failed),
		// this will prevent storing state.
		// An error should have been set during open().
		Debug(_log, "WLED Rest client not ready, cannot store state.");
		return false;
	}

	bool required = _isRestoreOrigState || _isSyncOverwrite || _isStreamToSegment;
	if (!required) {
		Debug(_log, "No conditions require storing WLED state.");
		_originalStateProperties = QJsonObject(); // Ensure it's cleared
		_wledInfo = QJsonObject();
		_currentVersion.clear();
		return true; // Nothing to do, so it's a "success"
	}

	QJsonObject fullState = _wledRestClient->getFullDeviceStateInfo();
	if (_wledRestClient->isInError() || fullState.isEmpty())
	{
		this->setInError(QString("WLED storeState failed: Could not retrieve device properties. Error: %1")
							 .arg(_wledRestClient->getErrorReason()));
		return false;
	}

	_originalStateProperties = fullState.value(WledConstants::API_PATH_STATE).toObject();
	_wledInfo = fullState.value(WledConstants::API_PATH_INFO).toObject();

	DebugIf(verbose, _log, "WLED Original State: [%s]", QSTRING_CSTR(QString(QJsonDocument(_originalStateProperties).toJson(QJsonDocument::Compact))));
	DebugIf(verbose, _log, "WLED Info: [%s]", QSTRING_CSTR(QString(QJsonDocument(_wledInfo).toJson(QJsonDocument::Compact))));

	// Parse and store UdpN settings from original state if sync overwrite is enabled
	if (_isSyncOverwrite || _isRestoreOrigState) // Restore needs original udpn too
	{
		QJsonObject udpn = _originalStateProperties.value(WledConstants::STATE_UDPN).toObject();
		if (!udpn.isEmpty())
		{
			_originalStateUdpnSend = udpn.value(WledConstants::STATE_UDPN_SEND).toBool(false);
			_originalStateUdpnRecv = udpn.value(WledConstants::STATE_UDPN_RECV).toBool(true); // Default true
			Debug(_log, "Stored original WLED UdpN state: Send=%s, Recv=%s",
				  _originalStateUdpnSend ? "true" : "false",
				  _originalStateUdpnRecv ? "true" : "false");
		} else {
			Warning(_log, "WLED UdpN object not found in state, using defaults for restore (Send=false, Recv=true)");
			_originalStateUdpnSend = false;
			_originalStateUdpnRecv = true;
		}
	}

	// Parse and store current WLED version
	QString versionStr = _wledInfo.value(WledConstants::INFO_VER).toString();
	if (!versionStr.isEmpty())
	{
		_currentVersion.setVersion(versionStr.toStdString());
		if (!_currentVersion.isValid()) {
			Warning(_log, "Failed to parse WLED version string: %s. Version-dependent features might not work as expected.", QSTRING_CSTR(versionStr));
		} else {
			Debug(_log, "WLED version: %s", QSTRING_CSTR(versionStr));
		}
	} else {
		Warning(_log, "WLED version string is empty in info object. Version checks will fail.");
		_currentVersion.clear();
	}
	return true;
}

bool LedDeviceWled::restoreState()
{
	if (!_wledRestClient || !_wledRestClient->isDeviceReady())
	{
		Debug(_log, "WLED Rest client not ready, cannot restore state.");
		return false;
	}

	if (!_isRestoreOrigState)
	{
		Debug(_log, "Restore original state is disabled by configuration.");
		return true; // Not an error, just nothing to do.
	}

	if (_originalStateProperties.isEmpty()) {
		Warning(_log, "Original WLED state is empty, cannot restore. Was storeState successful?");
		// Not setting device in error, as this might be part of a normal shutdown sequence
		// where state wasn't stored (e.g. if device was never fully on).
		return false; // Or true, depending on desired strictness. Let's say false as it's an unexpected state.
	}

	// The WledRestClient::restoreStateWled method now expects the full original state.
	// It will internally handle setting "live":false and "tt":0.
	// It also handles segment-specific restoration logic.
	bool success = _wledRestClient->restoreStateWled(
		_originalStateProperties,
		_isStreamToSegment,
		_streamSegmentId,
		_isStayOnAfterStreaming
		// Note: _originalStateUdpnSend & _originalStateUdpnRecv are part of _originalStateProperties if _isSyncOverwrite was true during store.
		// WledRestClient::restoreStateWled should ideally take the *entire* original state and just modify `live`, `tt`, and potentially `on` based on `_isStayOnAfterStreaming`.
		// If `_isSyncOverwrite` was false during `powerOn`, then udpn should not be touched by `restoreState` either, unless it's part of the `_originalStateProperties`.
		// The current WledRestClient::restoreStateWled seems to expect the full original object.
	);

	if (!success) {
		Warning(_log, "WLED restoreState command failed: %s", QSTRING_CSTR(_wledRestClient->getErrorReason()));
	} else {
		Debug(_log, "WLED state restored successfully.");
	}
	return success;
}

QJsonObject LedDeviceWled::discover(const QJsonObject& /*params*/)
{
	QJsonObject devicesDiscovered;
	devicesDiscovered.insert("ledDeviceType", _activeDeviceType );
	QJsonArray deviceList;

#ifdef ENABLE_MDNS
	// This part remains largely the same as mDNS discovery is independent of internal WLED communication method
	QString discoveryMethod("mDNS");
	deviceList = MdnsBrowser::getInstance().data()->getServicesDiscoveredJson(
					 MdnsServiceRegister::getServiceType(_activeDeviceType),
					 MdnsServiceRegister::getServiceNameFilter(_activeDeviceType),
					 DEFAULT_DISCOVER_TIMEOUT // Ensure this constant is defined or use a local one
					 );
	devicesDiscovered.insert("discoveryMethod", discoveryMethod);
#else
	Warning(_log, "WLED discovery via mDNS is not enabled in this build of Hyperion.");
#endif
	devicesDiscovered.insert("devices", deviceList);
	DebugIf(verbose, _log, "WLED devicesDiscovered: [%s]", QSTRING_CSTR(QString(QJsonDocument(devicesDiscovered).toJson(QJsonDocument::Compact))));
	return devicesDiscovered;
}

QJsonObject LedDeviceWled::getProperties(const QJsonObject& params)
{
	DebugIf(verbose, _log, "getProperties params: [%s]", QSTRING_CSTR(QString(QJsonDocument(params).toJson(QJsonDocument::Compact))));
	QJsonObject properties; // This will be the overall return object e.g. { "properties": { ... }, "error": "..." }

	QString host = params[CONFIG_HOST].toString();
	if (host.isEmpty()) {
		properties.insert("error", "Hostname/IP address is empty in parameters for getProperties.");
		Warning(_log, "getProperties called with empty host parameter.");
		return properties;
	}

	// Use a temporary WledRestClient for this, as getProperties is often called for discovery/setup
	// before the main device instance is fully initialized or opened.
	// It should not rely on the main _wledRestClient instance of an active LedDeviceWled.
	int apiPortForGetProperties = WledConstants::API_DEFAULT_PORT; // Default, NetUtils will handle if "host:port"
	QHostAddress resolvedAddrForGetProps;

	if (!NetUtils::resolveHostToAddress(_log, host, resolvedAddrForGetProps, apiPortForGetProperties)) {
		QString errorMsg = QString("Failed to resolve hostname '%1' for getProperties.").arg(host);
		Warning(_log, "%s", QSTRING_CSTR(errorMsg));
		properties.insert("error", errorMsg);
		return properties;
	}

	WledRestClient tempRestClient(resolvedAddrForGetProps.toString(), apiPortForGetProperties, _log);
	if (!tempRestClient.open()) {
		QString errorMsg = QString("Failed to connect to WLED API at %1:%2 for getProperties. Error: %3")
								.arg(resolvedAddrForGetProps.toString()).arg(apiPortForGetProperties).arg(tempRestClient.getErrorReason());
		Warning(_log, "%s", QSTRING_CSTR(errorMsg));
		properties.insert("error", errorMsg);
		return properties;
	}

	QJsonObject fullDeviceStateInfo = tempRestClient.getFullDeviceStateInfo();
	if (tempRestClient.isInError() || fullDeviceStateInfo.isEmpty()) {
		QString errorMsg = QString("Failed to retrieve properties from %1:%2. Error: %3")
								.arg(resolvedAddrForGetProps.toString()).arg(apiPortForGetProperties).arg(tempRestClient.getErrorReason());
		Warning(_log, "%s", QSTRING_CSTR(errorMsg));
		properties.insert("error", errorMsg);
		return properties;
	}

	// Check DDP stream readiness and add maxLedCount if UDP-Raw might be implicitly chosen by older WLED versions
	QJsonObject infoPart = fullDeviceStateInfo.value(WledConstants::API_PATH_INFO).toObject();
	semver::version tempVersion(infoPart.value(WledConstants::INFO_VER).toString("0.0.0").toStdString());

	QJsonObject reportedProperties = fullDeviceStateInfo; // Start with the full state/info

	// The original getProperties added a "maxLedCount" if DDP was not supported.
	// We need to decide if this logic is still relevant or if client should choose protocol based on this.
	// For now, replicate existing behavior of adding info if DDP isn't supported by the version.
	if (tempVersion.isValid() && tempVersion < semver::version(WLED_VERSION_DDP_SUPPORT))
	{
		if (!reportedProperties.isEmpty()) // Ensure there's an object to insert into
		{
			// This implies that if DDP is not supported, RAW is the only option, which has a LED limit.
			reportedProperties.insert("maxLedCountNote", QString("WLED version %1 does not support DDP. Max LEDs for UDP-RAW is %2.")
														.arg(QString::fromStdString(tempVersion.getVersion()))
														.arg(UDP_RAW_MAX_LED_NUM));
			reportedProperties.insert("maxLedCount", UDP_RAW_MAX_LED_NUM);
		}
	}
    // The 'filter' parameter from original getProperties is not used here as WledRestClient::getFullDeviceStateInfo gets all.
    // If filtering is needed, it should be done here on `reportedProperties` or WledRestClient needs a method that accepts a path.

	properties.insert("properties", reportedProperties);
	DebugIf(verbose, _log, "getProperties result: [%s]", QSTRING_CSTR(QString(QJsonDocument(properties).toJson(QJsonDocument::Compact))));
	return properties;
}

void LedDeviceWled::identify(const QJsonObject& params)
{
	DebugIf(verbose, _log, "identify params: [%s]", QSTRING_CSTR(QString(QJsonDocument(params).toJson(QJsonDocument::Compact))));

	QString host = params[CONFIG_HOST].toString();
	if (host.isEmpty()) {
		Warning(_log, "Identify called with empty host parameter.");
		return;
	}

	int apiPortForIdentify = WledConstants::API_DEFAULT_PORT;
	QHostAddress resolvedAddrForIdentify;

	if (!NetUtils::resolveHostToAddress(_log, host, resolvedAddrForIdentify, apiPortForIdentify)) {
		Warning(_log, "Failed to resolve hostname '%1' for identify.", QSTRING_CSTR(host));
		return;
	}

	// Use a temporary WledRestClient for identify action
	WledRestClient tempRestClient(resolvedAddrForIdentify.toString(), apiPortForIdentify, _log);
	if (!tempRestClient.open()) {
		Warning(_log, "Failed to connect to WLED API at %1:%2 for identify. Error: %3",
				QSTRING_CSTR(resolvedAddrForIdentify.toString()), apiPortForIdentify, QSTRING_CSTR(tempRestClient.getErrorReason()));
		return;
	}

	Info(_log, "Identifying WLED device at %s", QSTRING_CSTR(host));

	// Store current state of the target device (using the temporary client)
	QJsonObject originalStateToRestore;
	bool shouldRestore = params.value("restoreOriginalState").toBool(true); // Default to true if not specified

	if (shouldRestore) {
		originalStateToRestore = tempRestClient.getFullDeviceStateInfo().value(WledConstants::API_PATH_STATE).toObject();
		if (tempRestClient.isInError() || originalStateToRestore.isEmpty()) {
			Warning(_log, "Could not retrieve original state for %s before identify. Identify will proceed without restore. Error: %s",
					QSTRING_CSTR(host), QSTRING_CSTR(tempRestClient.getErrorReason()));
			shouldRestore = false; // Can't restore if we couldn't get it
			originalStateToRestore = QJsonObject(); // Clear it
		}
	}

	int segmentIdForIdentify = params.value(CONFIG_STREAM_SEGMENT_ID).toInt(0); // Default to segment 0

	if (!tempRestClient.identifyWled(segmentIdForIdentify, originalStateToRestore, shouldRestore)) {
		Warning(_log, "WLED identify command failed for %s: %s", QSTRING_CSTR(host), QSTRING_CSTR(tempRestClient.getErrorReason()));
	} else {
		Info(_log, "WLED device %s identified.", QSTRING_CSTR(host));
	}
}

int LedDeviceWled::write(const std::vector<ColorRgb> &ledValues)
{
	if (!_isDeviceReady)
	{
		// Device not ready (either streaming client or REST client failed to open, or connection lost)
		// Debug message can be added here if needed, but setInError should have been called already.
		return -1;
	}

	int rc = -1;
	if (_isStreamDDP)
	{
		if (_ddpClient && _ddpClient->isDeviceReady())
		{
			rc = _ddpClient->sendDdpPacket(ledValues, static_cast<int>(_ledCount));
			if (rc != 0 && _ddpClient->isDeviceInError())
			{
				this->setInError(QString("WLED DDP send error: %1").arg(_ddpClient->getError()));
			}
		}
		else
		{
			this->setInError("WLED DDP client not ready or not initialized for writing.");
			rc = -1;
		}
	}
	else // UDP-RAW
	{
		if (_rawClient && _rawClient->isDeviceReady())
		{
			rc = _rawClient->sendRawPacket(ledValues, static_cast<int>(_ledRGBCount));
			if (rc != 0 && _rawClient->isDeviceInError())
			{
				this->setInError(QString("WLED Raw UDP send error: %1").arg(_rawClient->getError()));
			}
		}
		else
		{
			this->setInError("WLED Raw UDP client not ready or not initialized for writing.");
			rc = -1;
		}
	}
	return rc;
}
