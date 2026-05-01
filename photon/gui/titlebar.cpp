#include "titlebar.hpp"
void TitleBar::clearInteract(){
    interactiveRectCount = 0;
    for (SDL_Rect& rect : interactiveRects)
        rect = SDL_Rect{0, 0, 0, 0};
}

void TitleBar::addInteract(const ImVec2& min, const ImVec2& max){
    if (interactiveRectCount >= buttonCount) return;
    SDL_Rect& rect = interactiveRects[interactiveRectCount++];
    rect.x = static_cast<int>(min.x);
    rect.y = static_cast<int>(min.y);
    rect.w = static_cast<int>(max.x - min.x);
    rect.h = static_cast<int>(max.y - min.y);
}

bool TitleBar::isInteract(int x, int y) const {
    for (int i = 0; i < interactiveRectCount; ++i) {
        const SDL_Rect& rect = interactiveRects[i];
        if ((x >= rect.x) && (x < rect.x + rect.w) &&
            (y >= rect.y) && (y < rect.y + rect.h))
            return true;
    }
    return false;
}
