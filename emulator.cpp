#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

void write_serial(const std::string& port, double temp) {
#ifdef _WIN32
    HANDLE hSerial = CreateFileA(port.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hSerial != INVALID_HANDLE_VALUE) {
        std::string data = std::to_string(temp) + "\n";
        DWORD written;
        WriteFile(hSerial, data.c_str(), data.length(), &written, NULL);
        CloseHandle(hSerial);
    }
#else
    int fd = open(port.c_str(), O_WRONLY | O_NOCTTY);
    if (fd != -1) {
        std::string data = std::to_string(temp) + "\n";
        write(fd, data.c_str(), data.length());
        close(fd);
    }
#endif
}

int main() {
    std::string portName;
    std::cout << "Enter Serial Port to WRITE (e.g. COM1 or /dev/pts/1): ";
    std::cin >> portName;

    while (true) {
        double temp = 20.0 + (rand() % 100) / 10.0; // 20.0 - 30.0
        std::cout << "Sending Temp: " << temp << std::endl;
        write_serial(portName, temp);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}