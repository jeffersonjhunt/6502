#include "keyboard.h"

namespace ooe
{
    Keyboard::Keyboard(bool blocking)
    {
        old_t = {0};
        if (tcgetattr(STDIN_FILENO, &old_t) < 0)
                perror("tcgetattr()");

        struct termios new_t = old_t;

        new_t.c_lflag &= ~ICANON;
        new_t.c_lflag &= ~ECHO;
        new_t.c_cc[VMIN] = blocking ? 1 : 0;
        new_t.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSANOW, &new_t) < 0)
            perror("tcsetattr ICANON");
    }

    Keyboard::~Keyboard()
    {
        std::cout << "Keyboard::~Keyboard() restoring terminal settings" << std::endl;
        if (tcsetattr(STDIN_FILENO, TCSADRAIN, &old_t) < 0)
            perror ("tcsetattr ~ICANON");
    }

    uint8_t Keyboard::Pull()
    {
        char buf = 0;

        if (read(STDIN_FILENO, &buf, 1) < 0)
                perror ("read()");
        
        return (buf);
    }

} // namespace ooe