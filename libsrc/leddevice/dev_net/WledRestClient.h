// clang-format off
#ifndef WLEDRESTCLIENT_H
#define WLEDRESTCLIENT_H

#include "ProviderRestApi.h"
#include <utils/Logger.h>
// #include <utils/version.hpp> // Not directly used by WledRestClient's interface
#include <QJsonObject>
#include <QString>
#include <QJsonArray>
#include <chrono> // For std::chrono::milliseconds in WledConstants

// Constants previously in LedDeviceWled.cpp that are relevant for REST API interaction
namespace WledConstants {
    const char API_BASE_PATH[] = "/json/";
    const char API_PATH_STATE[] = "state";
    const char API_PATH_INFO[] = "info";

    // List of State keys
    const char STATE_ON[] = "on";
    const char STATE_BRI[] = "bri";
    const char STATE_LIVE[] = "live";
    const char STATE_LOR[] = "lor";
    const char STATE_SEG[] = "seg";
    const char STATE_SEG_ID[] = "id";
    const char STATE_SEG_LEN[] = "len";
    const char STATE_SEG_FX[] = "fx";
    const char STATE_SEG_SX[] = "sx";
    const char STATE_MAINSEG[] = "mainseg";
    const char STATE_UDPN[] = "udpn";
    const char STATE_UDPN_SEND[] = "send";
    const char STATE_UDPN_RECV[] = "recv";
    const char STATE_TRANSITIONTIME_CURRENTCALL[] = "tt";

    // List of Info keys
    const char INFO_VER[] = "ver";
    const char INFO_LIVESEG[] = "liveseg";

    const int API_DEFAULT_PORT = -1; // Use default port per communication scheme
    constexpr std::chrono::milliseconds DEFAULT_IDENTIFY_TIME{ 2000 };
} // namespace WledConstants

class WledRestClient : public QObject
{
    Q_OBJECT

public:
    WledRestClient(const QString& hostAddress, int apiPort, Logger* logger, QObject* parent = nullptr);
    ~WledRestClient() override;

    // Initialize and open the REST API connection
    bool open();

    // Method to send a generic state update request
    // Returns true on success, false on failure (and sets error)
    bool sendStateUpdateRequest(const QJsonObject &request, const QString& requestType = "");

    // Method to get all properties (state and info)
    // Returns the full JSON response object, or an empty object on error
    QJsonObject getFullDeviceStateInfo();

    // Specific WLED actions
    bool powerOnWled(bool isStreamToSegment, const QJsonObject& originalStateProperties, const QJsonObject& wledInfo,
                     int streamSegmentId, bool isSwitchOffOtherSegments, int ledCount,
                     bool isBrightnessOverwrite, int brightness, bool isSyncOverwrite);

    bool powerOffWled(bool isStreamToSegment, int streamSegmentId, bool isStayOnAfterStreaming,
                      bool isSyncOverwrite, bool originalStateUdpnSend, bool originalStateUdpnRecv);

    bool restoreStateWled(const QJsonObject& originalStateProperties, bool isStreamToSegment, int streamSegmentId, bool isStayOnAfterStreaming);

    bool identifyWled(int streamSegmentId, const QJsonObject& originalStatePropertiesToRestore, bool restoreOrigState);

    QString getErrorReason() const { return _errorReason; }
    bool isInError() const { return _isInError; }
    void setLogger(Logger* logger);


    // Helper to construct a segment object for API calls
    static QJsonObject getSegmentObject(int segmentId, bool isOn, int brightness = -1);
    // Helper to construct a udpn object for API calls
    static QJsonObject getUdpnObject(bool isSendOn, bool isRecvOn);


private:
    ProviderRestApi* _restApi;
    Logger* _log;
    QString _hostAddress;
    int _apiPort;

    bool _isDeviceReady;
    bool _isInError;
    QString _errorReason;

    void setError(const QString& reason);
    void clearError();
};

#endif // WLEDRESTCLIENT_H
// clang-format on
