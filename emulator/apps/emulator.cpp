#include "emulator.h"
#include "wozmon.h"

namespace ooe
{

    Emulator *emulator;

    void Wrapper_WriteMemory(uint16_t address, uint8_t data){
        emulator->WriteMemory(address, data, true);
    }

    uint8_t Wrapper_ReadMemory(uint16_t address){
        return emulator->ReadMemory(address, true);
    }

    /**
     * @brief bootstraps the application
     */
    extern "C" int main()
    {
        // create the emulator
        emulator = new Emulator(true);
        
        mos6502 *cpu = new mos6502(&Wrapper_ReadMemory, &Wrapper_WriteMemory, nullptr);
        cpu->Reset();

        // run the emulator
        emulator->Run(cpu);
    }

    void Emulator::WriteMemory(uint16_t address, uint8_t data, bool debug)
    {
        if (debug)
            std::cout << fmt::format("Emulator::WriteMemory() {:#06x} <- {:#04x}", address, data) << std::endl;
        this->memory[address] = data;
    }

    uint8_t Emulator::ReadMemory(uint16_t address, bool debug)
    {
        if (debug)
            std::cout << fmt::format("ReadMemory() {:#06x} -> {:#04x}", address, this->memory[address]) << std::endl;
        return this->memory[address];
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

    void Emulator::Run(mos6502 *cpu)
    {
        while (true)
        {       
            // update display
            this->UpdateDisplay();

            // read keyboard
            this->ReadKeyboard();
            
            // step the cpu
            cpu->Run(1, this->cycleCount);

            // ticks
            if(this->cycleCount % 1000 == 0){
                std::cout << "Ticks: " << this->cycleCount << std::endl;
            } 

            if(this->shouldPause)
                std::this_thread::sleep_for(std::chrono::milliseconds(THROTTLE));
        }
    }

    Emulator::Emulator(bool shouldPause)
    {
        this->shouldPause = shouldPause;

        std::cout << "Emulator::Emulator() initializing memory" << std::endl;
        for (int i = 0; i < MEM_SIZE; i++)
        {
            this->WriteMemory(i,  0x00);
        }
        std::cout << "Emulator::Emulator() memory initialized" << std::endl;

        // load the WozMon ROM
        this->WozMon(0xFF00);

        std::cout << "Emulator::Emulator() RESET vector now at 0xFFFC -> " <<  fmt::format("{:#04x}",this->ReadMemory(0xFFFC)) << std::endl;
        std::cout << "Emulator::Emulator() RESET vector now at 0xFFFD -> " <<  fmt::format("{:#04x}",this->ReadMemory(0xFFFD)) << std::endl;

        this->cycleCount = 0;

        std::cout << "Emulator::Emulator() init keyboard monitor" << std::endl;
        keyboard = new Keyboard();
        std::cout << "Emulator::Emulator() keyboard monitor initialized" << std::endl;
    }   

    Emulator::~Emulator()
    {
        delete keyboard;
        delete emulator;
    }

    void Emulator::WozMon(uint16_t address){
        std::cout << "Emulator::WozMon() loading WozMon ROM starting at " <<  fmt::format("{:#06x}",address) << std::endl;
  
        for (int i = 0; i < sizeof(WOZMON); i++)
        {
            this->WriteMemory(address+i, WOZMON[i]);
        }

        std::cout << "Emulator::WozMon() Loaded Wozmon into memory" << std::endl;
    }

} // namespace ooe