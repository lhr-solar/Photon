#include "serial.hpp"
#include <cstddef>
#include <cstring>
#include <mutex>
#include <system_error>
#include <stdexcept>
#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#endif
#include "ringbuffer.hpp"
#include <string>

#ifdef _WIN32

SerialPort::SerialPort(const std::string& portName, unsigned baudRate) {
  // Windows COM names ≥ COM10 need the \\.\ prefix
  std::string name = (portName.size() > 4 && portName.rfind("COM", 0) == 0)
                     ? std::string{"\\\\.\\"} + portName
                     : portName;

  _handle = CreateFileA(
    name.c_str(),
    GENERIC_READ | GENERIC_WRITE,
    0,//FILE_SHARE_READ | FILE_SHARE_WRITE,
    nullptr,          // default security
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    nullptr
  );
  if (_handle == INVALID_HANDLE_VALUE) {
      //OutputDebugString(std::to_string(_handle).c_str()));
      //OutputDebugString("[!] Failed to create serial connection!\n");
      std::string s = std::to_string((int)_handle);
      //OutputDebugString(s.c_str());
      //OutputDebugString(" \n");
    //throw std::system_error(GetLastError(), std::system_category(), "CreateFileA failed");
  }

  // Configure baud, parity, stop bits, data bits
  DCB dcb = {};
  dcb.DCBlength = sizeof(dcb);
  if (!GetCommState(_handle, &dcb)) {
    CloseHandle(_handle);
    throw std::system_error(GetLastError(), std::system_category(), "GetCommState failed");
  }
  dcb.BaudRate = baudRate;
  dcb.ByteSize = 8;
  dcb.Parity   = NOPARITY;
  dcb.StopBits = ONESTOPBIT;

  // disable all flow control & special processing
  dcb.fBinary = TRUE;    // binary mode, no EOF check
  dcb.fParity = FALSE;   // no parity checking
  dcb.fOutxCtsFlow = FALSE;   // no CTS output flow control
  dcb.fOutxDsrFlow = FALSE;   // no DSR output flow control
  dcb.fDtrControl = DTR_CONTROL_DISABLE;
  dcb.fDsrSensitivity = FALSE;
  dcb.fTXContinueOnXoff = FALSE;   // no XOFF holding
  dcb.fOutX = FALSE;   // no XON/XOFF out
  dcb.fInX = FALSE;   // no XON/XOFF in
  dcb.fErrorChar = FALSE;
  dcb.fNull = FALSE;
  dcb.fRtsControl = RTS_CONTROL_DISABLE;
  dcb.fAbortOnError = FALSE;

  if (!SetCommState(_handle, &dcb)) {
    CloseHandle(_handle);
    throw std::system_error(GetLastError(), std::system_category(), "SetCommState failed");
  }

  // Blocking reads that return as soon as 1+ bytes are available
  COMMTIMEOUTS timeouts = {};
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.ReadTotalTimeoutConstant = 0;
  timeouts.WriteTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 0;

  if (!SetCommTimeouts(_handle, &timeouts)) {
    CloseHandle(_handle);
    throw std::system_error(GetLastError(), std::system_category(), "SetCommTimeouts failed");
  }
}

SerialPort::~SerialPort() {
    if (_handle != INVALID_HANDLE_VALUE) {
        CancelIoEx(_handle, nullptr);
        PurgeComm(_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
        CloseHandle(_handle);
    }
}

std::size_t SerialPort::read(uint8_t* buf, std::size_t maxlen) {
  DWORD got = 0;
  if (!ReadFile(_handle, buf, static_cast<DWORD>(maxlen), &got, nullptr)) {
    throw std::system_error(GetLastError(), std::system_category(), "ReadFile failed");
  }
  return static_cast<std::size_t>(got);
}

void SerialPort::write(const uint8_t* buf, std::size_t len) {
  DWORD sent = 0;
  if (!WriteFile(_handle, buf, static_cast<DWORD>(len), &sent, nullptr)) {
    throw std::system_error(GetLastError(), std::system_category(), "WriteFile failed");
  }
  if (sent != len) {
    throw std::runtime_error("WriteFile: incomplete write");
  }
}

#else

SerialPort::SerialPort(const std::string& portName, unsigned baudRate){
    _fd = ::open(portName.c_str(), O_RDWR | O_NOCTTY);
    if (_fd < 0)
        throw std::system_error(errno, std::system_category(), "tcgetattr failed");

    struct termios tty = {};
    if (tcgetattr(_fd, &tty) != 0){
        ::close(_fd);
        throw std::system_error(errno, std::system_category(), "tcgetattr failed");
    }

    cfmakeraw(&tty);
    speed_t speed;
    switch (baudRate) {
      case  9600: speed = B9600;   break;
      case 19200: speed = B19200;  break;
      case 38400: speed = B38400;  break;
      case 57600: speed = B57600;  break;
      case 115200: speed = B115200; break;
      default:
        ::close(_fd);
        throw std::invalid_argument("Unsupported baud rate");
      }
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 0;

    if(tcsetattr(_fd, TCSANOW, &tty) != 0){
        ::close(_fd);
        throw std::system_error(errno, std::system_category(), "tcsetattr failed");
    }

    tcflush(_fd, TCIFLUSH);
}

SerialPort::~SerialPort(){
    if (_fd >= 0) ::close(_fd);
}

std::size_t SerialPort::read(uint8_t* buf, std::size_t maxlen) {
  ssize_t n = ::read(_fd, buf, maxlen);
  return static_cast<std::size_t>(n);
}

void SerialPort::write(const uint8_t* buf, std::size_t len) {
  [[maybe_unused]] ssize_t n = ::write(_fd, buf, len);
}

#endif
