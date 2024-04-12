#include <iostream>

#include <Windows.h>
#include <TlHelp32.h>

#include "client.dll.hpp"
#include "offsets.hpp"

static DWORD get_process_id(const wchar_t* process_name) {
    DWORD process_id = 0;

    HANDLE snap_shot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if (snap_shot == INVALID_HANDLE_VALUE)
        return process_id;

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(decltype(entry));

    if (Process32FirstW(snap_shot, &entry) == TRUE) {
        // Check if the first handle is the one we want.
        if (_wcsicmp(process_name, entry.szExeFile) == 0)
            process_id = entry.th32ProcessID;
        else {
            while (Process32NextW(snap_shot, &entry) == TRUE) {
                if (_wcsicmp(process_name, entry.szExeFile) == 0) {
                    process_id = entry.th32ProcessID;
                    break;
                }
            }
        }
    }

    CloseHandle(snap_shot);

    return process_id;
}

static std::uintptr_t get_module_base(const DWORD pid, const wchar_t* module_name) {
    std::uintptr_t module_base = 0;

    // Snap-shot of process' modules (dlls).
    HANDLE snap_shot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap_shot == INVALID_HANDLE_VALUE)
        return module_base;

    MODULEENTRY32W entry = {};
    entry.dwSize = sizeof(decltype(entry));

    if (Module32FirstW(snap_shot, &entry) == TRUE) {
        if (wcsstr(module_name, entry.szModule) != nullptr)
            module_base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
        else {
            while (Module32NextW(snap_shot, &entry) == TRUE) {
                if (wcsstr(module_name, entry.szModule) != nullptr) {
                    module_base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
                    break;
                }
            }
        }
    }

    CloseHandle(snap_shot);

    return module_base;
}

namespace driver {
    namespace codes {
        // Used to setup the driver.
        constexpr ULONG attach =
            CTL_CODE(FILE_DEVICE_UNKNOWN, 0x696, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

        // Read process memory.
        constexpr ULONG read =
            CTL_CODE(FILE_DEVICE_UNKNOWN, 0x697, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

        // Read process memory.
        constexpr ULONG write =
            CTL_CODE(FILE_DEVICE_UNKNOWN, 0x698, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
    }  // namespace codes

    // Shared between user mode & kernel mode.
    struct Request {
        HANDLE process_id;

        PVOID target;
        PVOID buffer;

        SIZE_T size;
        SIZE_T return_size;
    };

    bool attach_to_process(HANDLE driver_handle, const DWORD pid) {
        Request r;
        r.process_id = reinterpret_cast<HANDLE>(pid);

        return DeviceIoControl(driver_handle, codes::attach, &r, sizeof(r), &r, sizeof(r), nullptr,
            nullptr);
    }

    template <class T>
    T read_memory(HANDLE driver_handle, const std::uintptr_t addr) {
        T temp = {};

        Request r;
        r.target = reinterpret_cast<PVOID>(addr);
        r.buffer = &temp;
        r.size = sizeof(T);

        DeviceIoControl(driver_handle, codes::read, &r, sizeof(r), &r, sizeof(r), nullptr, nullptr);

        return temp;
    }

    template <class T>
    void write_memory(HANDLE driver_handle, const std::uintptr_t addr, const T& value) {
        Request r;
        r.target = reinterpret_cast<PVOID>(addr);
        r.buffer = (PVOID)&value;
        r.size = sizeof(T);

        DeviceIoControl(driver_handle, codes::write, &r, sizeof(r), &r, sizeof(r), nullptr,
            nullptr);
    }
}  // namespace driver

namespace offsets {
    int dwLocalPlayerPawn = 0x17361E8;
    int dwForceJump = 0x1730530;

    int dwEntityList = 0x18C1DB8;//cs2_dumper::offsets::client_dll::dwEntityList;

    //da client.dll.hpp
    int m_hPlayerPawn = 0x7E4;

    int m_iszPlayerName = 0x638;
    int m_entitySpottedState = 0x1698;
    int m_bSpotted = 0x8;
    int m_iHealth = 0x334;
    int m_bDormant = 0xE7;
    int m_ListEntry = 0x3A8;
    int m_flDetectedByEnemySensorTime = 0x1440;
}

int main() {
    const DWORD pid = get_process_id(L"cs2.exe");

    if (pid == 0) {
        std::cout << "Failed to find cs2.\n";
        std::cin.get();
        return 1;
    }

    const HANDLE driver = CreateFile(L"\\\\.\\driver_peso", GENERIC_READ, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (driver == INVALID_HANDLE_VALUE) {
        std::cout << "Failed to create our driver handle.\n";
        std::cin.get();
        return 1;
    }

    int radar_hack = 0;
    int glow_hack = 0;

    std::cout << "Do you want Radar hack? [1/0]\n";
    std::cin >> radar_hack;

    std::cout << "Do you want glow-hack? [1/0]\n";
    std::cin >> glow_hack;

    if (radar_hack == 0 && glow_hack == 0) {
        std::cout << "Sorry you did not select any cheat. exiting.........\n";
        exit(0);
    }

    if (driver::attach_to_process(driver, pid) == true) {
        std::cout << "Attachment successful.\n";

        if (const std::uintptr_t client = get_module_base(pid, L"client.dll"); client != 0) {
            std::cout << "Client found.\n";

            float last_glow = 0;

            while (true) {

                const auto local_player_pawn = driver::read_memory<std::uintptr_t>(
                    driver, client + offsets::dwLocalPlayerPawn);


                const auto entity_list = driver::read_memory<std::uintptr_t>(
                    driver, client + offsets::dwEntityList);

                printf("entity_list: %p\n", entity_list);

                const auto list_entry = driver::read_memory<std::uintptr_t>(
                    driver, entity_list + 0x10);

                printf("list_entry: %p\n", list_entry);

                for (int i = 0; i < 64; ++i) {

                    if (list_entry == 0) {
                        continue;
                    }


                    const auto current_controller = driver::read_memory<std::uintptr_t>(
                        driver, list_entry + i * 0x78);

                    if (current_controller == 0) {
                        continue;
                    }


                    const auto pawn_handle = driver::read_memory<std::uintptr_t>(
                        driver, current_controller + offsets::m_hPlayerPawn);

                    if (pawn_handle == 0) {
                        continue;
                    }

                    const auto list_entry2 = driver::read_memory<std::uintptr_t>(
                        driver, entity_list + 0x8 * ((pawn_handle & 0x7FFF) >> 9) + 0x10);

                    const auto current_pawn = driver::read_memory<std::uintptr_t>(
                        driver, list_entry2 + 0x78 * (pawn_handle & 0x1FF));

                    int health = driver::read_memory<int>(
                        driver, current_pawn + offsets::m_iHealth);



                    bool spotted = driver::read_memory<bool>(
                        driver, current_pawn + offsets::m_entitySpottedState + offsets::m_bSpotted);
                    

                    //const auto nome = driver::read_memory<std::uintptr_t>(driver, current_controller + offsets::m_iszPlayerName);
                    //printf("name: %s   HEALTH: %d\n", nome,health);
                    printf("HEALTH: %d -- spotted: %d\n", health, spotted);

                    //printf("spotted: %d\n", spotted);
                    //void write_memory(HANDLE driver_handle, const std::uintptr_t addr, const T & value) {


                    if (radar_hack) {
                        //radar hack (scrivo il bool in memoria)
                        driver::write_memory<bool>(driver, current_pawn + offsets::m_entitySpottedState + offsets::m_bSpotted, true);
                    }


                    float last_glow_copy = driver::read_memory<float>(driver, current_pawn + offsets::m_flDetectedByEnemySensorTime);

                    if (!glow_hack && last_glow_copy == 86400) {
                        driver::write_memory<float>(driver, current_pawn + offsets::m_flDetectedByEnemySensorTime, 0);
                    }
                    if (glow_hack) {
                        //glow hack, scrivo il colore dei nemici
                        driver::write_memory<float>(driver, current_pawn + offsets::m_flDetectedByEnemySensorTime, 86400);
                    }


                }
                Sleep(200);


            }
        }
    }

    CloseHandle(driver);

    std::cin.get();

    return 0;
}
