#include "serial.hpp"
#include <string>

/*
void Serial::consumer(){
    std::string canId;
    std::string value;
    bool collectingCAN = false;
    bool collectingVal = false;
    while(running){
        if(unsigned char* l = serialQueue.front()){
            serialQueue.pop();
            if((*l == 'g') || (*l == 'a')){
                canId.clear();
                value.clear();
                collectingCAN = true;
                collectingVal = false;
                canId.push_back(*l);
                continue;
            }
            if(collectingCAN && *l == ' '){
                canId.push_back(*l);
                collectingCAN = false;
                collectingVal = true;
                continue;
            }
            if(collectingCAN){
                canId.push_back(*l);
            }
            if(collectingVal && ((*l == ' ') || (*l == '\n'))){
                int v;
                auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), v);
                if (ec == std::errc()) writeSample(retCANID(canId.c_str()), v);
                canId.clear(); value.clear(); collectingVal = false;
            }
            if(collectingVal){
                value.push_back(*l);
            }
        }
    }
}
*/
