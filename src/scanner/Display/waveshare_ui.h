#pragma once

#include <Arduino.h>

constexpr size_t DISPLAY_TEXT_SIZE = 96;
constexpr size_t DISPLAY_PID_SLOTS = 5;

enum class DisplayPage : uint8_t
{
	Overview = 0,
	LivePids,
	VehicleInfo,
	Faults,
	Help,
};

enum class WaveshareUiAction : uint8_t
{
	None = 0,
	NextPage,
	PreviousPage,
	Refresh,
};

struct DisplayPidSlot
{
	bool valid = false;
	char label[24] = {};
	char value[48] = {};
};

struct DisplayFrame
{
	DisplayPage currentPage = DisplayPage::Overview;
	char pageName[24] = "Overview";
	char diagnosticBus[16] = "None";
	bool can1Started = false;
	bool can2Started = false;
	char can1Bitrate[24] = "offline";
	char can2Bitrate[24] = "offline";
	char vin[24] = "unknown";
	uint8_t dtcCount = 0;
	char lastDtc[12] = "none";
	char status[DISPLAY_TEXT_SIZE] = "booting";
	DisplayPidSlot pids[DISPLAY_PID_SLOTS] = {};
};

class DisplayManager
{
public:
	bool begin(Stream& logStream);
	bool available() const;
	void render(const DisplayFrame& frame, Stream& logStream);
	WaveshareUiAction pollAction(Stream& logStream);
};