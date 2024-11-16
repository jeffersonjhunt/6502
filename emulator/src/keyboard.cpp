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

    /**
     * @brief check if a key was pressed and if so, write it to the data address
     * 
     * note: the data available flag is set in the control register but cleared by the read in the bus
     * 
     * @return uint8_t 
     */
    uint8_t Keyboard::Pull()
    {
        char buf = 0;

        if (read(STDIN_FILENO, &buf, 1) < 0) {
                LOG(ERROR) << "read() failed";
        }

        u_int8_t ctrl = bus->Read(this->ctrl_addr);

        // if a key was pressed and the data available flag is not set
        if (buf != 0 && !bus->bit_test(ctrl, 7))
        {
            bus->Write(data_addr, EncodeKey(buf));
            bus->Write(ctrl_addr, bus->bit_set(ctrl, 7)); // set the data available flag
        } else if (buf != 0) {
            LOG(WARNING) << fmt::format("Key: {:#04x} (IGNORED)", buf);
            buf = 0;
        }

        return (buf);
    }

    /**
     * TODO: this shoudl be an interface for different encoding schemes
     * for now its just Apple 1 encoding
     */
    uint8_t Keyboard::EncodeKey(uint8_t key)
    {
        // adjust case, apple 1 is upper case only
        if(key >= 'a' && key <= 'z')
        {
            key -= 0x20;
        }

        switch (key)
        {
            case 0x0a: // CR -> LF
                key = 0x0d;
            default:
                // set the high bit
                return bus->bit_set(key, 7);
        }
    }

} // namespace ooe