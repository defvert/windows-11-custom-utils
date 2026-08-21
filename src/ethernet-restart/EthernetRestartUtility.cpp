#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

int RunCommand(const std::string& command)
{
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    std::string cmd = "cmd.exe /C " + command;

    if (!CreateProcessA(
        nullptr,
        cmd.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi))
    {
        return static_cast<int>(GetLastError());
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return static_cast<int>(exitCode);
}

int main()
{
    // Устанавливаем UTF-8 для консоли Windows
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Отключаем буферизацию stdout
    std::cout.setf(std::ios::unitbuf);

    std::cout << "Перезапуск сетевого адаптера Ethernet...\n";

    const std::string adapterName = "Ethernet";

    std::string disableCommand =
        "netsh interface set interface name=\"" +
        adapterName +
        "\" admin=disabled";

    int disableResult = RunCommand(disableCommand);

    if (disableResult != 0)
    {
        std::cout
            << "При отключении адаптера произошла ошибка: "
            << disableResult
            << "\n";
    }
    else
    {
        std::cout << "Адаптер отключён. Ожидание...\n";

        std::this_thread::sleep_for(std::chrono::seconds(2));

        std::string enableCommand =
            "netsh interface set interface name=\"" +
            adapterName +
            "\" admin=enabled";

        int enableResult = RunCommand(enableCommand);

        if (enableResult != 0)
        {
            std::cout
                << "При включении адаптера произошла ошибка: "
                << enableResult
                << "\n";
        }
        else
        {
            std::cout
                << "Сетевой адаптер успешно был перезапущен\n";
        }
    }

    std::cout << "Окно закроется через 5 секунд...\n";

    std::this_thread::sleep_for(std::chrono::seconds(5));

    return 0;
}