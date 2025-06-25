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
// Large (25%)- likely to have max 1 per screen, maybe for a large visual,
// Medium (12.5%)- likely to have many per screen,
// small (6.25%)- likely only a few
//
//
// Limits
// 4 Large
// 8 Medium
// 16 Small
// attach a value to each size, if the size is greater than 100, then continue
// creation on a new tab
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

