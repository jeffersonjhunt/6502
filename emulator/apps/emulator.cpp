#include "emulator.h"
#include "wozmon.h"

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
        Keyboard *keyboard = new Keyboard(memory, IN);

        // LOG(DEBUG) << "initializing display";
        // Display *display = new Display(memory, DSP);

        LOG(DEBUG) << "initializing emulator";
        Emulator *emulator = new Emulator(memory, keyboard);
        
        LOG(DEBUG) << "resetting cpu";
        mos6502->Reset();

        LOG(DEBUG) << "running emulator";
        emulator->Run(mos6502);
    }

    void Emulator::UpdateDisplay()
    {
        // update the display
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
                std::exit(0);
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
            // update display
            this->UpdateDisplay();

            // read keyboard
            this->ReadKeyboard();
            
            // step the cpu
            mos6502->Step();

            // ticks
            this->ticks++;
            if(this->ticks % 1000 == 0){
                LOG(DEBUG) << "Ticks: " << this->ticks;
            } 
            std::this_thread::sleep_for(std::chrono::milliseconds(TICK_RATE));
        }
    }

    Emulator::Emulator(Memory *memory, Keyboard *keyboard)
    {
        this->memory = memory;
        this->keyboard = keyboard;

        // load the WozMon ROM (move to storage class)
        this->WozMon(0xFF00);

        this->ticks = 0;
    }   

    Emulator::~Emulator()
    {
        delete keyboard;
        delete memory;
    }

    void Emulator::WozMon(uint16_t address)
    {
        LOG(INFO) << "Emulator::WozMon() loading WozMon";
        for (int i = 0; i < sizeof(WOZMON); i++)
        {
            this->memory->Write(address+i, WOZMON[i]);
        }
        LOG(DEBUG) << fmt::format("Emulator::WozMon() Loaded Wozmon into memory starting at {:#06x}", address);
    }

} // namespace ooe