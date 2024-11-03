#include "emulator.h"
#include "wozmon.h"

namespace ooe
{

    Emulator *emulator;
    Keyboard *keyboard;

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
        emulator = new Emulator();
        
        mos6502 *cpu = new mos6502(&Wrapper_ReadMemory, &Wrapper_WriteMemory, nullptr);
        cpu->Reset();

        // run the emulator
        emulator->Run(cpu);
    }

    void Emulator::WriteMemory(uint16_t address, uint8_t data, bool debug)
    {
        if (debug)
            std::cout << "WriteMemory: " << fmt::format("{:#06x}",address) << " " <<  fmt::format("{:#04x}", data) << std::endl;
        this->memory[address] = data;
    }

    uint8_t Emulator::ReadMemory(uint16_t address, bool debug)
    {
        if (debug)
            std::cout << "ReadMemory: " << fmt::format("{:#06x}",address) << " " << fmt::format("{:#04x}", this->memory[address]) << std::endl;
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
            if(this->lastCycleCount != this->cycleCount){
                std::cout << "Ticks: " << this->cycleCount << std::endl;
                this->lastCycleCount = this->cycleCount;
            } 
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    Emulator::Emulator()
    {
        std::cout << "Emulator::Emulator() initializing memory" << std::endl;
        for (int i = 0; i < MEM_SIZE; i++)
        {
            this->WriteMemory(i,  0x00);
        }
        std::cout << "Emulator::Emulator() memory initialized" << std::endl;

        // load the WozMon ROM
        this->WozMon(0xFF00);   

        // set the reset vector
        this->WriteMemory(0xFFFC, 0x00);
        this->WriteMemory(0xFFFD, 0xFF);

        this->cycleCount = 0;
        this->lastCycleCount = 0;

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
  
        for (int i = 0; i < sizeof(wozmon); i++)
        {
            this->WriteMemory(address+i, wozmon[i]);
        }

        std::cout << "Emulator::WozMon() Loaded Wozmon into memory" << std::endl;
    }

} // namespace ooe