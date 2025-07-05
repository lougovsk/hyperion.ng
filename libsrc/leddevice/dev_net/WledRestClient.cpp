// clang-format off
#include "WledRestClient.h"
#include <utils/JsonUtils.h> // For QJsonDocument
#include <utils/WaitTime.h> // For wait()

// Constants
namespace {
    const bool verbose = false; // For DebugIf
} //End of constants

WledRestClient::WledRestClient(const QString& hostAddress, int apiPort, Logger* logger, QObject* parent)
    : QObject(parent)
    , _restApi(nullptr)
    , _log(logger)
    , _hostAddress(hostAddress)
    , _apiPort(apiPort)
    , _isDeviceReady(false)
    , _isInError(false)
{
    if (_log == nullptr)
    {
        // Fallback to a default logger instance if none provided, though it's better if LedDeviceWled provides one
        _log = Logger::getInstance("WLED_REST_CLIENT");
        Warning(_log, "No logger provided to WledRestClient, using default instance.");
    }
}

WledRestClient::~WledRestClient()
{
    delete _restApi;
    _restApi = nullptr;
}

void WledRestClient::setLogger(Logger* logger)
{
    _log = logger;
    if(_restApi != nullptr)
    {
        _restApi->setLogger(logger);
    }
}

bool WledRestClient::open()
{
    clearError();
    if (_restApi == nullptr)
    {
        _restApi = new ProviderRestApi(_hostAddress, _apiPort);
        if (_log) {
            _restApi->setLogger(_log);
        }
    }
    else
    {
        _restApi->setHost(_hostAddress);
        _restApi->setPort(_apiPort);
    }
    _restApi->setBasePath(WledConstants::API_BASE_PATH);

    // Try a simple GET to see if the device is reachable
    _restApi->setPath(WledConstants::API_PATH_INFO);
    httpResponse response = _restApi->get();
    if (response.error())
    {
        setError(QString("Failed to connect to WLED device at %1:%2. Error: %3")
                     .arg(_hostAddress)
                     .arg(_apiPort)
                     .arg(response.getErrorReason()));
        _isDeviceReady = false;
        return false;
    }

    _isDeviceReady = true;
    return true;
}

void WledRestClient::setError(const QString& reason)
{
    _isInError = true;
    _errorReason = reason;
    if (_log) Error(_log, "%s", QSTRING_CSTR(reason));
}

void WledRestClient::clearError()
{
    _isInError = false;
    _errorReason.clear();
}

bool WledRestClient::sendStateUpdateRequest(const QJsonObject &request, const QString requestType)
{
    if (!_isDeviceReady)
    {
        setError(QString("WLED Rest Client not ready. Cannot send %1 request.").arg(requestType));
        return false;
    }
    clearError();

    _restApi->setPath(WledConstants::API_PATH_STATE);
    DebugIf(verbose, _log, "Sending WLED state update request (%s): %s", QSTRING_CSTR(requestType), QSTRING_CSTR(QString(QJsonDocument(request).toJson(QJsonDocument::Compact))));

    httpResponse response = _restApi->put(request);
    if (response.error())
    {
        setError(QString("%1 request failed with error: '%2'").arg(requestType, response.getErrorReason()));
        return false;
    }
    // Optionally, check response.getBody() for success indication if API provides one
    return true;
}

QJsonObject WledRestClient::getFullDeviceStateInfo()
{
    if (!_isDeviceReady) {
        setError("WLED Rest Client not ready. Cannot get device state/info.");
        return QJsonObject();
    }
    clearError();

    _restApi->setPath(""); // Query root to get both state and info

    httpResponse response = _restApi->get();
    if (response.error())
    {
        setError(QString("Retrieving device properties failed with error: '%1'").arg(response.getErrorReason()));
        return QJsonObject();
    }
    return response.getBody().object();
}

QJsonObject WledRestClient::getSegmentObject(int segmentId, bool isOn, int brightness)
{
    QJsonObject segmentObj
    {
        {WledConstants::STATE_SEG_ID, segmentId},
        {WledConstants::STATE_ON, isOn}
    };

    if (brightness > -1)
    {
        segmentObj.insert(WledConstants::STATE_BRI, brightness);
    }
    return segmentObj;
}

QJsonObject WledRestClient::getUdpnObject(bool isSendOn, bool isRecvOn)
{
    QJsonObject udpnObj
    {
        {WledConstants::STATE_UDPN_SEND, isSendOn},
        {WledConstants::STATE_UDPN_RECV, isRecvOn}
    };
    return udpnObj;
}

bool WledRestClient::powerOnWled(bool isStreamToSegment, const QJsonObject& originalStateProperties, const QJsonObject& wledInfo,
                                 int streamSegmentId, bool isSwitchOffOtherSegments, int ledCount,
                                 bool isBrightnessOverwrite, int brightness, bool isSyncOverwrite)
{
    if (!_isDeviceReady) return false;

    QJsonObject cmd;
    if (isStreamToSegment)
    {
        // This logic requires originalStateProperties and wledInfo to be fresh
        // Consider if WledRestClient should fetch them if not provided or stale
        const QJsonArray propertiesSegments = originalStateProperties.value(WledConstants::STATE_SEG).toArray();
        bool isStreamSegmentIdFound { false };
        QJsonArray segments;

        for (const auto& segmentItem : propertiesSegments)
        {
            QJsonObject segmentObj = segmentItem.toObject();
            int segmentID = segmentObj.value(WledConstants::STATE_SEG_ID).toInt();

            if (segmentID == streamSegmentId)
            {
                isStreamSegmentIdFound = true;
                int len = segmentObj.value(WledConstants::STATE_SEG_LEN).toInt(0); // Default to 0 if not found
                if (len > 0 && ledCount > len) // Check len > 0 to avoid division by zero or illogical error if segment has no length
                {
                     setError(QString("Too many LEDs [%1] configured for segment [%2], which supports maximum [%3] LEDs. Check your WLED setup!").arg(ledCount).arg(streamSegmentId).arg(len));
                     return false;
                }

                int segBrightness = -1;
                if (isBrightnessOverwrite)
                {
                    segBrightness = brightness;
                }
                segments.append(getSegmentObject(segmentID, true, segBrightness));
            }
            else if (isSwitchOffOtherSegments)
            {
                segments.append(getSegmentObject(segmentID, false));
            }
        }

        if (!isStreamSegmentIdFound)
        {
            setError(QString("Segment streaming to segment [%1] configured, but segment does not exist on WLED. Check your WLED setup!").arg(streamSegmentId));
            return false;
        }
        cmd.insert(WledConstants::STATE_SEG, segments);
        cmd.insert(WledConstants::STATE_MAINSEG, streamSegmentId);
    }
    else // Not streaming to a specific segment
    {
        if (isBrightnessOverwrite)
        {
            cmd.insert(WledConstants::STATE_BRI, brightness);
        }
    }

    cmd.insert(WledConstants::STATE_LIVE, true);
    cmd.insert(WledConstants::STATE_ON, true);

    if (isSyncOverwrite)
    {
        if (_log) Debug(_log, "Disable synchronisation with other WLED devices via WledRestClient");
        cmd.insert(WledConstants::STATE_UDPN, getUdpnObject(false, false));
    }

    return sendStateUpdateRequest(cmd, "Power-on");
}

bool WledRestClient::powerOffWled(bool isStreamToSegment, int streamSegmentId, bool isStayOnAfterStreaming,
                                  bool isSyncOverwrite, bool originalStateUdpnSend, bool originalStateUdpnRecv)
{
    if (!_isDeviceReady) return false;

    QJsonObject cmd;
    if (isStreamToSegment)
    {
        QJsonArray segments;
        segments.append(getSegmentObject(streamSegmentId, isStayOnAfterStreaming));
        cmd.insert(WledConstants::STATE_SEG, segments);
    }

    cmd.insert(WledConstants::STATE_LIVE, false);
    cmd.insert(WledConstants::STATE_TRANSITIONTIME_CURRENTCALL, 0); // Apply immediately
    cmd.insert(WledConstants::STATE_ON, isStayOnAfterStreaming);

    if (isSyncOverwrite)
    {
        if (_log) Debug(_log, "Restore synchronisation with other WLED devices via WledRestClient");
        cmd.insert(WledConstants::STATE_UDPN, getUdpnObject(originalStateUdpnSend, originalStateUdpnRecv));
    }

    return sendStateUpdateRequest(cmd, "Power-off");
}

bool WledRestClient::restoreStateWled(const QJsonObject& originalStateProperties, bool isStreamToSegment, int streamSegmentId, bool isStayOnAfterStreaming)
{
    if (!_isDeviceReady) return false;

    QJsonObject stateToRestore = originalStateProperties; // Make a mutable copy

    if (isStreamToSegment)
    {
        const QJsonArray currentSegmentsArray = stateToRestore.value(WledConstants::STATE_SEG).toArray();
        QJsonArray newSegmentsArray;
        for (const auto& segmentItem : currentSegmentsArray)
        {
            QJsonObject segmentObj = segmentItem.toObject();
            int segmentID = segmentObj.value(WledConstants::STATE_SEG_ID).toInt();
            if (segmentID == streamSegmentId)
            {
                // Modify only the target segment's 'on' state based on _isStayOnAfterStreaming
                // Other properties of the segment (like effect, brightness etc.) remain as they were in _originalStateProperties
                segmentObj[WledConstants::STATE_ON] = isStayOnAfterStreaming;
            }
            newSegmentsArray.append(segmentObj);
        }
        stateToRestore[WledConstants::STATE_SEG] = newSegmentsArray;
    }

    stateToRestore[WledConstants::STATE_LIVE] = false;
    stateToRestore[WledConstants::STATE_TRANSITIONTIME_CURRENTCALL] = 0; // Apply immediately

    // If not streaming to a segment, or if global 'on' state needs to be set
    if (isStayOnAfterStreaming) {
         stateToRestore[WledConstants::STATE_ON] = true;
    } else if (!isStreamToSegment) { // Only set global 'on' to false if not streaming to segment (segment logic handles its own 'on')
         stateToRestore[WledConstants::STATE_ON] = false;
    }


    return sendStateUpdateRequest(stateToRestore, "Restore-state");
}

bool WledRestClient::identifyWled(int streamSegmentId, const QJsonObject& originalStatePropertiesToRestore, bool restoreOrigState)
{
    if (!_isDeviceReady) return false;

    QJsonObject cmd;
    cmd.insert(WledConstants::STATE_ON, true);
    cmd.insert(WledConstants::STATE_LOR, 1); // Live override set to 1 (until reboot)

    QJsonObject segmentSetup = getSegmentObject(streamSegmentId, true, 255); // Max brightness for identify
    segmentSetup.insert(WledConstants::STATE_SEG_FX, 25); // Effect "Chase"
    segmentSetup.insert(WledConstants::STATE_SEG_SX, 128); // Effect speed

    QJsonArray segments;
    segments.append(segmentSetup);
    cmd.insert(WledConstants::STATE_SEG, segments);

    if (!sendStateUpdateRequest(cmd, "Identify-start"))
    {
        return false;
    }

    wait(WledConstants::DEFAULT_IDENTIFY_TIME);

    if (restoreOrigState)
    {
        // Restore the original state (which should include STATE_LIVE:false)
        // The originalStatePropertiesToRestore should be the state *before* identify started.
        // For simplicity, we assume it contains { "on": original_on_state, "bri": original_bri, "seg": [...] }
        // We need to ensure STATE_LIVE is set to false.
        QJsonObject stateToRestore = originalStatePropertiesToRestore;
        stateToRestore[WledConstants::STATE_LIVE] = false; // Ensure live override is turned off
        stateToRestore[WledConstants::STATE_LOR] = 0; // Turn off live override
        stateToRestore[WledConstants::STATE_TRANSITIONTIME_CURRENTCALL] = 0; // Apply immediately

        // If specific segment was targeted, ensure its state is part of restore logic
        // This part might need more careful handling if identify modified segments not intended.
        // The simplest restore is to send the entire original state object.

        return sendStateUpdateRequest(stateToRestore, "Identify-restore");
    }
    else
    {
        // If not restoring, at least turn off the live override and the effect
        QJsonObject identifyEndCmd;
        identifyEndCmd.insert(WledConstants::STATE_LOR, 0);
        identifyEndCmd.insert(WledConstants::STATE_LIVE, false); // Turn off live streaming mode
        QJsonObject segmentOff = getSegmentObject(streamSegmentId, true); // Keep it on, but remove effect
        segmentOff.insert(WledConstants::STATE_SEG_FX, 0); // Solid effect
        QJsonArray segmentsOff;
        segmentsOff.append(segmentOff);
        identifyEndCmd.insert(WledConstants::STATE_SEG, segmentsOff);
        return sendStateUpdateRequest(identifyEndCmd, "Identify-end");
    }
}

// clang-format on
