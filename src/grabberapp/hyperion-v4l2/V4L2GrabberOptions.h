#pragma once

#include <limits>

#include <QString>

#include <hyperion/GrabberWrapper.h>
#include <utils/PixelFormat.h>
#include <utils/VideoStandard.h>

struct V4L2GrabberOptions
{
	QString device = QStringLiteral("auto");
	int input = 0;
	VideoStandard videoStandard = VideoStandard::NO_CHANGE;
	PixelFormat pixelFormat = PixelFormat::NO_CHANGE;
	int fps = GrabberWrapper::DEFAULT_RATE_HZ;
	FlipMode flipMode = FlipMode::NO_CHANGE;
	int width = 640;
	int height = 480;
	int sizeDecimation = GrabberWrapper::DEFAULT_PIXELDECIMATION;
	int cropLeft = 0;
	int cropRight = 0;
	int cropTop = 0;
	int cropBottom = 0;

	bool video3DSBS = false;
	bool video3DTAB = false;

	// Hardware picture controls of the capture device (the value range is device specific).
	// A control that is not set stays at NO_CHANGE: this is outside of any device's value range,
	// which makes V4L2Grabber leave the control as it is (see V4L2Wrapper).
	static constexpr int HARDWARE_CONTROL_NO_CHANGE = std::numeric_limits<int>::min();
	int brightness = HARDWARE_CONTROL_NO_CHANGE;
	int contrast = HARDWARE_CONTROL_NO_CHANGE;
	int saturation = HARDWARE_CONTROL_NO_CHANGE;
	int hue = HARDWARE_CONTROL_NO_CHANGE;

	bool signalDetectionEnabled = true;
	double signalThresholdRed = 0.0;
	double signalThresholdGreen = 0.0;
	double signalThresholdBlue = 0.0;

	// Only applied when the user supplied all four --signal-[horizontal|vertical]-[min|max] options
	bool applySignalDetectionOffset = false;
	double signalHorizontalMin = 0.0;
	double signalVerticalMin = 0.0;
	double signalHorizontalMax = 0.0;
	double signalVerticalMax = 0.0;

	bool skipReply = false;
	bool screenshot = false;
	bool debug = false;
	bool help = false;
	
	QString address;
	int priority = 150;

	// Set by parseV4L2GrabberOptions() when CLI validation fails (e.g. incompatible
	// signal-area options); checked first thing in V4L2GrabberTraits::run().
	bool valid = true;
	QString error;
};
