#include "emulator.h"

namespace ooe
{
    namespace
    {
        // Raw mode keeps ISIG, so Ctrl-C arrives as SIGINT rather than as a
        // 0x03 byte. Left to the default action it killed the process before
        // the keyboard could restore the terminal; now it just ends Run().
        volatile std::sig_atomic_t quit_requested = 0;

        void RequestQuit(int)
        {
            quit_requested = 1;
        }
    }

    /**
     * @brief bootstraps the application
     *
     * usage: emulator [rom]   (default: ibasic.rom in the current directory)
     */
    extern "C" int main(int argc, char* argv[])
    {
        START_EASYLOGGINGPP(argc, argv);

        // Load configuration from file
        el::Configurations conf("logging.cfg");
        el::Loggers::reconfigureAllLoggers(conf);

        const std::string rom = argc > 1 ? argv[1] : "ibasic.rom";

        LOG(DEBUG) << "initializing memory";
        Memory memory;

        // Before the keyboard takes over the terminal, so the error is
        // readable and there is nothing to restore.
        if (!LoadApple1(memory, rom))
        {
            std::fprintf(stderr, "emulator: cannot load ROM %s\n", rom.c_str());
            return 1;
        }

        LOG(DEBUG) << "initializing cpu";
        cpu mos6502(&memory);

        LOG(DEBUG) << "initializing keyboard";
        Keyboard keyboard(&memory, KBDCR, KBD);

        LOG(DEBUG) << "initializing display";
        Display display(&memory, DSPCR, DSP);

        LOG(DEBUG) << "initializing emulator";
        Emulator emulator(&memory, &keyboard, &display);

        std::signal(SIGINT, RequestQuit);
        std::signal(SIGTERM, RequestQuit);

        LOG(DEBUG) << "resetting cpu";
        mos6502.Reset();

        LOG(DEBUG) << "running emulator";
        emulator.Run(&mos6502);

        LOG(DEBUG) << "exiting";
        return 0;
    }

    void Emulator::UpdateDisplay()
    {
        this->display->update();
    }

    bool Emulator::ReadKeyboard()
    {
        // read the keyboard
        uint8_t key = keyboard->Pull();
        switch (key)
        {
            case 0x00:
                break;
            case 0x0a:
            case 0x0d:
                LOG(DEBUG) << fmt::format("Key: {:#04x} (RETURN)", key);
                break;
            case 0x20:
                LOG(DEBUG) << fmt::format("Key: {:#04x} (SPACE)", key);
                break;
            case 0x03:
                LOG(DEBUG) << fmt::format("Key: {:#04x} (CTRL-C)", key);
                return false;
            case 0x1b:
                LOG(DEBUG) << fmt::format("Key: {:#04x} (ESCAPE)", key);
                // std::exit(0);
                break;
            default:
                LOG(DEBUG) << fmt::format("Key: {:#04x} ({})", key, (char)key); 
                break;  
        }
        return true;
    }

    void Emulator::Run(cpu *mos6502)
    {
        while (!quit_requested)
        {
            // read keyboard
            if (!this->ReadKeyboard())
                break;

            // step the cpu
            for(int i = 0; i < CYCLES_PER_TICK; i++)
                mos6502->Step();

            // update display
            this->UpdateDisplay();            

            // ticks
            this->ticks++;
            if(this->ticks % 1000 == 0){
                LOG(TRACE) << "Ticks: " << this->ticks;
            } 
            if(TICK_RATE > 0)
                std::this_thread::sleep_for(std::chrono::microseconds(TICK_RATE));
        }
    }

    Emulator::Emulator(Memory *memory, Keyboard *keyboard, Display *display)
    {
        this->memory = memory;
        this->keyboard = keyboard;
        this->display = display;
        this->ticks = 0;
    }   

    Emulator::~Emulator()
    {
    }
} // namespace ooe
