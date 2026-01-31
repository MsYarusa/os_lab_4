#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <numeric>
#include <thread>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#endif

const std::string LOG_RAW_24H     = "raw_24h.log";
const std::string LOG_HOURLY_MON  = "hourly_month.log";
const std::string LOG_DAILY_YEAR  = "daily_year.log";

const int RAW_KEEP_HOURS = 24;  
const int HOURLY_KEEP_HOURS = 720;
const int DAILY_KEEP_DAYS = 365;

std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::chrono::system_clock::time_point parse_log_time(const std::string& line) {
    if (line.length() < 19) return std::chrono::system_clock::time_point::min();
    std::tm tm = {};
    std::istringstream ss(line.substr(0, 19));
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) return std::chrono::system_clock::time_point::min();
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

void rotate_log(const std::string& filename, int hours_to_keep) {
    std::vector<std::string> lines_to_keep;
    std::string line;
    
    auto cutoff_time = std::chrono::system_clock::now() - std::chrono::hours(hours_to_keep);
    
    std::ifstream in(filename);
    if (!in.is_open()) return;
    
    while (std::getline(in, line)) {
        if (parse_log_time(line) > cutoff_time) {
            lines_to_keep.push_back(line);
        }
    }
    in.close();

    std::ofstream out(filename, std::ios::trunc);
    for (const auto& l : lines_to_keep) out << l << "\n";
}

#ifdef _WIN32
HANDLE open_and_config_port(const std::string& portName) {
    HANDLE h = CreateFileA(portName.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return h;
    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(h, &dcb);
    dcb.BaudRate = CBR_9600;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    SetCommState(h, &dcb);
    COMMTIMEOUTS ct = {0};
    ct.ReadIntervalTimeout = 50;
    ct.ReadTotalTimeoutConstant = 50;
    ct.ReadTotalTimeoutMultiplier = 10;
    SetCommTimeouts(h, &ct);
    return h;
}
#else
int open_and_config_port(const std::string& portName) {
    int fd = open(portName.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd == -1) return -1;
    struct termios tty;
    tcgetattr(fd, &tty);
    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);
    tty.c_cflag |= (CLOCAL | CREAD | CS8);
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}
#endif

int main() {
    std::string portName;
    std::cout << "Enter Serial Port (e.g. COM3 or /dev/ttyUSB0): ";
    std::cin >> portName;

    std::vector<double> hour_buffer;
    std::vector<double> day_buffer;
    std::string accumulator = "";
    auto last_hour = std::chrono::system_clock::now();
    auto last_day = std::chrono::system_clock::now();

#ifdef _WIN32
    HANDLE hSerial = open_and_config_port(portName);
    if (hSerial == INVALID_HANDLE_VALUE) { std::cerr << "Error opening port\n"; return 1; }
#else
    int fd = open_and_config_port(portName);
    if (fd == -1) { std::cerr << "Error opening port\n"; return 1; }
#endif

    std::cout << "Monitoring started..." << std::endl;

    while (true) {
        char buf[128];
        int bytes_read = 0;
#ifdef _WIN32
        DWORD dwRead;
        if (ReadFile(hSerial, buf, sizeof(buf) - 1, &dwRead, NULL) && dwRead > 0) bytes_read = (int)dwRead;
#else
        bytes_read = read(fd, buf, sizeof(buf) - 1);
#endif

        if (bytes_read > 0) {
            buf[bytes_read] = '\0';
            for (int i = 0; i < bytes_read; i++) {
                if (buf[i] == '\n') {
                    if (!accumulator.empty()) {
                        try {
                            double temp = std::stod(accumulator);
                            std::string ts = get_timestamp();
                            std::ofstream f(LOG_RAW_24H, std::ios::app);
                            f << ts << " " << std::fixed << std::setprecision(1) << temp << "\n";
                            std::cout << "[" << ts << "] Temp: " << temp << std::endl;
                            hour_buffer.push_back(temp);
                        } catch (...) {}
                        accumulator = "";
                    }
                } else if (buf[i] != '\r') {
                    accumulator += buf[i];
                }
            }
        }

        auto now = std::chrono::system_clock::now();

        if (std::chrono::duration_cast<std::chrono::minutes>(now - last_hour).count() >= 60) {
            if (!hour_buffer.empty()) {
                double avg = std::accumulate(hour_buffer.begin(), hour_buffer.end(), 0.0) / hour_buffer.size();
                std::ofstream f(LOG_HOURLY_MON, std::ios::app);
                f << get_timestamp() << " AVG_HOUR: " << std::fixed << std::setprecision(2) << avg << "\n";
                
                day_buffer.push_back(avg);
                hour_buffer.clear();
            }

            rotate_log(LOG_RAW_24H, 24); 
            
            last_hour = now;
        }

        if (std::chrono::duration_cast<std::chrono::hours>(now - last_day).count() >= 24) {
            if (!day_buffer.empty()) {
                double avg = std::accumulate(day_buffer.begin(), day_buffer.end(), 0.0) / day_buffer.size();
                std::ofstream f(LOG_DAILY_YEAR, std::ios::app);
                f << get_timestamp() << " AVG_DAY: " << std::fixed << std::setprecision(2) << avg << "\n";
                
                day_buffer.clear();
            }

            rotate_log(LOG_HOURLY_MON, 720);

            rotate_log(LOG_DAILY_YEAR, 8760);

            last_day = now;
        }

    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return 0;
}