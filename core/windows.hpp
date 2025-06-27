#include <cstdlib>
#include <imgui.h>
#include <implot.h>
#include <implot3d.h>
#include <memory>
#include <string>
#include <vector>

class Windows {
public:
  std::string parent_tab;
  std::vector<std::string> windows;
  bool initialized = false;

  //  Windows(const std::string& name) : parent_tab(name) {}

private:
};
// Window generation Algorithm:
// we have three different windows
// XL (50%) - for config windows, such as can dbc configuration,
// don't configure programatically lol
//
// Large (12.5%)- likely to have max 1 or 2 per screen, maybe for a large visual,
// Medium (6.25%)- likely to have many per screen,
// small (3.125%)- likely only a few
// at the moment when we draw plots inside the given windows, we just draw them over and over again in a list, and they end up taking the entire horizontal screen space, and just generate on top of each other like a list that you have to scroll through , however we would like for them
// to be limited to the same screen / window so you don't have to scroll. the amount of windows can be hard coded in within the individual x_window() methods, (i.e. controls_window(), prohelion_window()) (and is expected to be in the range of 4-16 individual plots).
// you can think of having plots be a specific size/area of the available window, e.g. 
// Large (12.5%)- likely to have max 1 or 2 per screen, maybe for a large visual,
// Medium (6.25%)- likely to have many per screen,
// small (3.125%)- likely only a few
// you should also be able to pick up and drag/rearange the individual plots to a users liking
//
//
//
// Limits
// 4 Large
// 8 Medium
// 16 Small
//
//
// CAN DBC has standerdized the following labels:
// BS_
// bus configuration, this can be used for paramatizing the CAN bus
// i.e. bus speed
// this can just be placed in a small window in the config tab
//
// BU_
// Bus Unites/Nodes, all ECU's on the BUS
// Every ECU receives an instance of class Windows, and is specified by parent_tab
// every class of Windows, or every ECU, receives a seperate tab
// i.e. the number of elements in BU_ corresponds to the number of elements in vector tabs
//
// BO_
// Corresponds to CAN message ID's, each of these can contain
// 1 or more pieces of data (signals, SO_)
// tie signals to a specific ECU
//
// SG_
// contains the meta data from a specific signal
// i.e. use BO_ to determine valid signals, and from which ECU
// SG_ contains decoding rules for every piece of data in BO_

