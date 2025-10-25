#include "videoElement.hpp"

Video::Video() {}

Video::~Video() {
    close();
}

bool Video::open(const std::string& path) {
    return decoder.open(path);
}

void Video::close() {
    decoder.close();
}

bool Video::updateFrame() {
    return decoder.decodeNextFrame(currentFrame);
}

const frame& Video::getFrame() const {
    return currentFrame;
}

int Video::width() const {
    return decoder.getWidth();
}

int Video::height() const {
    return decoder.getHeight();
}

double Video::frameDuration() const {
    return decoder.getFrameDuration();
}
