#include "keyboard.h"

namespace ooe
{
    struct termios old;

    Keyboard::Keyboard(bool blocking)
    {
        old = {0};
        if (tcgetattr(STDIN_FILENO, &old) < 0)
                perror("tcsetattr()");
        old.c_lflag &= ~ICANON;
        old.c_lflag &= ~ECHO;
        old.c_cc[VMIN] = blocking ? 1 : 0;
        old.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSANOW, &old) < 0)
            perror("tcsetattr ICANON");
    }

    Keyboard::~Keyboard()
    {
        std::cout << "Keyboard::~Keyboard() restoring terminal settings" << std::endl;
        old.c_lflag |= ICANON;
        old.c_lflag |= ECHO;
        if (tcsetattr(STDIN_FILENO, TCSADRAIN, &old) < 0)
            perror ("tcsetattr ~ICANON");
    }

    uint8_t Keyboard::Pull()
    {
        char buf = 0;

        if (read(0, &buf, 1) < 0)
                perror ("read()");
        
        return (buf);
    }

} // namespace ooe