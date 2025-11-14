#include "videoElement.hpp"
#include <iostream>

Video::Video()
    : m_width(0)
    , m_height(0)
    , m_duration(0.0)
{
}

Video::~Video() {
    close();
}

bool Video::open(const std::string& filePath) {
    close();
    
    if (!decoder.open(filePath)) {
        std::cerr << "[Video] Failed to open video file: " << filePath << std::endl;
        return false;
    }
    
    m_width = decoder.width();
    m_height = decoder.height();
    m_duration = decoder.duration();
    
    // Decode first frame immediately
    if (!decoder.decodeNextFrame(currentFrame)) {
        std::cerr << "[Video] Failed to decode first frame" << std::endl;
        close();
        return false;
    }
    
    std::cout << "[Video] Successfully loaded video: " << m_width << "x" << m_height 
              << " (" << m_duration << "s)" << std::endl;
    
    return true;
}

bool Video::updateFrame() {
    if (!decoder.isOpen()) {
        return false;
    }
    
    // Decode next frame
    if (!decoder.decodeNextFrame(currentFrame)) {
        // End of video - could loop here if desired
        return false;
    }
    
    return true;
}

void Video::close() {
    decoder.close();
    currentFrame.free();
    m_width = 0;
    m_height = 0;
    m_duration = 0.0;
}