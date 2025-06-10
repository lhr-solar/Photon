#pragma once

#include <string>
#include <cstdint>
#include <cstddef>
#include <sys/types.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <iostream>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <termios.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

class SerialPort {
    public: 
        SerialPort(const std::string& portName, unsigned baudRate);
        ~SerialPort();

        SerialPort(const SerialPort&) = delete;
        SerialPort& operator=(const SerialPort&) = delete;
        std::size_t read(uint8_t* buf, std::size_t maxlen);
        void write(const uint8_t* buf, std::size_t len);

private:
#ifdef _WIN32
    HANDLE _handle = INVALID_HANDLE_VAlUE;
#else
    int _fd = -1;
#endif
};
