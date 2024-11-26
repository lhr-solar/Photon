Stack:
C++
GLFW for window creation
Vulkan for graphics processing
Boost.Asio for networking and serial port comms
ImGui for interactive GUI

This dashboard will be used remotely by various members of my team, so it's important we have a universal and synchronized CAN DBC standard(?), i.e. if one member changes or creates an ID with some associated data/description, it should be universally visible by everyone on the dashboard. Additionally, it's important that we have some sort of remote storage solution for our data, so it can be accessed by any of our data science teams, which may be remote.

GLFW for window creation
Vulkan for rendering / graphics processing
Boost for reading data from a database over a network through our CAN DBC software, and reading CAN data from a serial port (also in CAN format)
ImGui for visualizing the data

That would be the current software for the database, however, I also want the Xbee stuff visualized as well
So we should have:
Xbee reading from CAN Bus, and writing it to a database over a network
(we will extend this later)




```plantuml
@startuml
package Photon{
node Vulkan{

}

node 

}

```
