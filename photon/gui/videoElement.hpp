#pragma once
#include "videoDecoder.hpp"
#include "frame.hpp"
#include <string>

class Video {
public:
    Video();
    ~Video();

    // Open a video file
    bool open(const std::string& filePath);
    
    // Decode and advance to next frame
    bool updateFrame();
    
    // Get current frame (read-only)
    const frame& getFrame() const { return currentFrame; }
    
    // Video properties
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    double duration() const { return m_duration; }
    double currentTime() const { return currentFrame.timestamp; }
    
    // Check if video is open
    bool isOpen() const { return decoder.isOpen(); }
    
    // Close video
    void close();

private:
    videoDecoder decoder;
    frame currentFrame;
    uint32_t m_width;
    uint32_t m_height;
    double m_duration;
};