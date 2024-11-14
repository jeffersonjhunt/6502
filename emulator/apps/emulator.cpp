#include "emulator.h"
#include "rom.h"

namespace ooe
{
    /**
     * @brief bootstraps the application
     */
    extern "C" int main(int argc, char* argv[])
    {
        START_EASYLOGGINGPP(argc, argv);

        // Load configuration from file
        el::Configurations conf("logging.cfg");
        el::Loggers::reconfigureAllLoggers(conf);

        LOG(DEBUG) << "initializing memory";
        Memory *memory = new Memory();

        LOG(DEBUG) << "initializing cpu";
        cpu *mos6502 = new cpu(memory);

        LOG(DEBUG) << "initializing keyboard";
        Keyboard *keyboard = new Keyboard(memory, KBDCR, KBD);

        LOG(DEBUG) << "initializing display";
        Display *display = new Display(memory, DSPCR, DSP);

        LOG(DEBUG) << "initializing emulator";
        Emulator *emulator = new Emulator(memory, keyboard, display);
        
        LOG(DEBUG) << "resetting cpu";
        mos6502->Reset();

        LOG(DEBUG) << "running emulator";
        emulator->Run(mos6502);
    }

    void Emulator::UpdateDisplay()
    {
        this->display->update();
    }

    void Emulator::ReadKeyboard()
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
                std::exit(0);
                break;
            case 0x1b:
                LOG(DEBUG) << fmt::format("Key: {:#04x} (ESCAPE)", key);
                // std::exit(0);
                break;
            default:
                LOG(DEBUG) << fmt::format("Key: {:#04x} ({})", key, (char)key); 
                break;  
        }
    }

    void Emulator::Run(cpu *mos6502)
    {
        while (true)
        {
            // read keyboard
            this->ReadKeyboard();

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

        // load the Apple 1 ROM (move to storage class)
        this->LoadROM(0x8000);

        this->ticks = 0;
    }   

    Emulator::~Emulator()
    {
        delete keyboard;
        delete memory;
    }

    void Emulator::LoadROM(uint16_t address)
    {
        LOG(INFO) << "loading Apple 1 ROM";
        for (int i = 0; i < sizeof(ROM); i++)
        {
            this->memory->Write(address+i, ROM[i]);
        }
        LOG(DEBUG) << fmt::format("Loaded ROM into memory starting at {:#06x}", address);
    }

    void Emulator::LoadProgram(uint16_t address, uint8_t *program, size_t size)
    {
        LOG(INFO) << "Emulator::LoadProgram() loading program";
        for (int i = 0; i < size; i++)
        {
            this->memory->Write(address+i, program[i]);
        }
        LOG(DEBUG) << fmt::format("Emulator::LoadProgram() Loaded program into memory starting at {:#06x}", address);
    }

} // namespace ooe