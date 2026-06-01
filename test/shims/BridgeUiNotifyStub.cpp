#include <cstdint>

class BridgeUi {
public:
    void notifyStatus(const char* text, std::uint16_t color);
};

void BridgeUi::notifyStatus(const char* text, std::uint16_t color)
{
    (void)text;
    (void)color;
}
