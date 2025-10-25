#pragma once
#include <string>
#include "frame.hpp"
#include "videoDecoder.hpp"

class Video {
public:
    Video();
    ~Video();

    bool open(const std::string& path);
    void close();
    bool updateFrame(); // Decodes next frame (call on each UI tick)

    const frame& getFrame() const;

    int width() const;
    int height() const;
    double frameDuration() const;

private:
    videoDecoder decoder;
    frame currentFrame;
};
