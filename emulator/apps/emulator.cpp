#include "emulator.h"
#include "wozmon.h"

namespace ooe
{
    /**
     * @brief bootstraps the application
     */
    extern "C" int main()
    {
        // create the memory
        Memory *memory = new Memory();

        // create the cpu
        cpu *mos6502 = new cpu(memory);

        // create the keyboard
        Keyboard *keyboard = new Keyboard(memory, IN);

        // create the display
        // Display *display = new Display(memory, DSP);

        // create the emulator
        Emulator *emulator = new Emulator(memory, keyboard, true);
        
        mos6502->Reset();

        // run the emulator
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
                std::cout << "Key: return: " << (int)key << ": " << fmt::format("{:#04x}", key) << " (RETURN)" << std::endl;
                break;
            case 0x20:
                std::cout << "Key: space: " << (int)key << ": " << fmt::format("{:#04x}", key) << " (SPACE)" << std::endl;
                break;
            case 0x03:
                std::cout << "Key: ctrl-c: " << (int)key << ": " << fmt::format("{:#04x}", key) << " (CTRL-C)" << std::endl;
                std::exit(0);
                break;
            case 0x1b:
                std::cout << "Key: escape: " << (int)key << ": " << fmt::format("{:#04x}", key) << " (ESCAPE)" << std::endl;
                std::exit(0);
                break;
            default:
                std::cout << "Key: " << key << ": " << (int)key << ": " << fmt::format("{:#04x}", key) << std::endl;
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
            this->cycleCount++;
            if(this->cycleCount % 1000 == 0){
                std::cout << "Ticks: " << this->cycleCount << std::endl;
            } 

            if(this->shouldPause)
                std::this_thread::sleep_for(std::chrono::milliseconds(THROTTLE));
        }
    }

    Emulator::Emulator(Memory *memory, Keyboard *keyboard, bool shouldPause)
    {
        this->memory = memory;
        this->keyboard = keyboard;
        this->shouldPause = shouldPause;

        // load the WozMon ROM
        this->WozMon(0xFF00);

        std::cout << "Emulator::Emulator() RESET vector now at 0xFFFC -> " <<  fmt::format("{:#04x}",this->memory->Read(0xFFFC)) << std::endl;
        std::cout << "Emulator::Emulator() RESET vector now at 0xFFFD -> " <<  fmt::format("{:#04x}",this->memory->Read(0xFFFD)) << std::endl;

        this->cycleCount = 0;
    }   

    Emulator::~Emulator()
    {
        delete keyboard;
    }

    void Emulator::WozMon(uint16_t address){
        std::cout << "Emulator::WozMon() loading WozMon ROM starting at " <<  fmt::format("{:#06x}",address) << std::endl;
  
        for (int i = 0; i < sizeof(WOZMON); i++)
        {
            this->memory->Write(address+i, WOZMON[i]);
        }

        std::cout << "Emulator::WozMon() Loaded Wozmon into memory" << std::endl;
    }

} // namespace ooe