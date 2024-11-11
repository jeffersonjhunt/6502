#include "keyboard.h"

namespace ooe
{
    Keyboard::Keyboard(BusInterface *bus, uint16_t ctrl_addr, uint16_t data_addr)
    {
        this->bus = bus;
        this->ctrl_addr = ctrl_addr;
        this->data_addr = data_addr;

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

        u_int8_t ctrl = bus->Read(this->ctrl_addr);

        // if a key was pressed
        if (buf != 0 && ctrl == 0x27 ) {

            if(buf == 0x0a) // LF -> CR
                buf = 0x0d;

            LOG(TRACE) << fmt::format("Raw Key: {:#04x}", buf);
            // update the bus with the new value adjusted for apple 1
            bus->Write(data_addr, bus->bit_set(buf, 7));
            LOG(TRACE) << fmt::format("Adj Key: {:#04x}", bus->Read(data_addr));
            bus->Write(ctrl_addr, 0xa7); // set the data available flag
        } 
        else {
            bus->Write(ctrl_addr, 0x27); // clear the data available flag
        }

        return (buf);
    }

    uint8_t Keyboard::DecodeKey(uint8_t key)
    {
        return key;
    }

} // namespace ooe