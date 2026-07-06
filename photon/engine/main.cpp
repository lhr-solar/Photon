#include "include.hpp"
#include "photon.hpp"

int main() {
  logs("Starting");
  Photon photon;
  photon.init();
  photon.renderLoop();
  photon.destroy();
  return 0;
}
