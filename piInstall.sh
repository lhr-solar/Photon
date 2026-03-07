#!/usr/bin/env bash

GREEN="\033[0;32m"
RED="\033[0;31m"
YELLOW="\033[1;33m"
RESET="\033[0m"

packages=(
    vulkan-tools
    libvulkan-dev
    vulkan-validationlayers-dev
    spirv-tools
    v4l-utils
    glslc
)

failed=()

for pkg in "${packages[@]}"; do
    echo -e "${YELLOW}Installing ${pkg}...${RESET}"

    if sudo apt install -y "$pkg"; then
        echo -e "${GREEN}SUCCESS: ${pkg}${RESET}"
    else
        echo -e "${RED}FAILED: ${pkg}${RESET}"
        failed+=("$pkg")
    fi

    echo
done

if [ ${#failed[@]} -ne 0 ]; then
    echo -e "${RED}Some packages failed to install.${RESET}"
    echo -e "${YELLOW}please attempt manual install of:${RESET}"
    for pkg in "${failed[@]}"; do
        echo -e "  ${RED}${pkg}${RESET}"
    done
else
    echo -e "${GREEN}All packages installed successfully.${RESET}"
fi
