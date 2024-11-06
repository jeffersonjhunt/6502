#include "keyboard.h"

namespace ooe
{
    Keyboard::Keyboard(BusInterface *bus, uint16_t buf_adr, uint8_t buf_size)
    {
        this->bus = bus;
        
        this->buf_adr = buf_adr;
        this->buf_size = buf_size;
        this->buf_ptr = buf_adr;

        LOG(DEBUG) << fmt::format("Buffer Address: {:#06x}", buf_adr);
        LOG(DEBUG) << fmt::format("Buffer Size:    {:#04x}", buf_size);
        LOG(DEBUG) << fmt::format("Buffer End:     {:#06x}", (buf_adr+buf_size));
        LOG(DEBUG) << fmt::format("Buffer Ptr:     {:#06x}", buf_ptr);

        old_t = {0};
        if (tcgetattr(STDIN_FILENO, &old_t) < 0) {
                LOG(ERROR) << "tcgetattr() failed";
        }

        struct termios new_t = old_t;

        new_t.c_lflag &= ~ICANON;
        new_t.c_lflag &= ~ECHO;
        new_t.c_cc[VMIN] = 0;
        new_t.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSANOW, &new_t) < 0) {
            LOG(ERROR) << "tcsetattr() failed";
        }
    }

    Keyboard::~Keyboard()
    {
        LOG(DEBUG) << "Keyboard::~Keyboard() restoring terminal settings";
        if (tcsetattr(STDIN_FILENO, TCSADRAIN, &old_t) < 0) {
            LOG(ERROR) << "tcsetattr() failed";
        }
    }

    uint8_t Keyboard::Pull()
    {
        char buf = 0;

        if (read(STDIN_FILENO, &buf, 1) < 0) {
                LOG(ERROR) << "read() failed";
        }

        // if a key was pressed
        if (buf != 0) { 
            // update the bus with the new value
            bus->Write(buf_ptr, buf);

            // increment the buffer pointer
            buf_ptr == (buf_adr+buf_size) ? buf_ptr = buf_adr : buf_ptr++;
        }

        return (buf);
    }

    uint8_t Keyboard::DecodeKey(uint8_t key)
    {
        return key;
    }

} // namespace ooe