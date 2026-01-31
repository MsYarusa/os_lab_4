#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <numeric> // Для std::accumulate
#include <thread>
#include <sstream> // Для парсинга строк

#ifdef _WIN32
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

// --- КОНСТАНТЫ (Имена файлов) ---
const std::string LOG_RAW_24H     = "raw_24h.log";
const std::string LOG_HOURLY_MON  = "hourly_month.log";
const std::string LOG_DAILY_YEAR  = "daily_year.log";

// --- Вспомогательные функции времени ---

// Получить текущее время строкой
std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// Парсинг времени из начала строки лога
// Ожидаемый формат начала строки: "2023-10-05 14:30:00 ..."
std::chrono::system_clock::time_point parse_log_time(const std::string& line) {
    if (line.length() < 19) return std::chrono::system_clock::time_point::min();

    std::tm tm = {};
    std::string time_part = line.substr(0, 19);
    std::istringstream ss(time_part);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    if (ss.fail()) return std::chrono::system_clock::time_point::min();

    // mktime конвертирует struct tm в time_t
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

// --- ЧЕСТНАЯ ОЧИСТКА ЛОГОВ ---
void rotate_log(const std::string& filename, int hours_to_keep) {
    std::vector<std::string> lines_to_keep;
    std::string line;
    
    // Вычисляем "время отсечения" (сейчас минус N часов)
    auto now = std::chrono::system_clock::now();
    auto cutoff_time = now - std::chrono::hours(hours_to_keep);

    std::ifstream in(filename);
    if (!in.is_open()) return;

    while (std::getline(in, line)) {
        auto record_time = parse_log_time(line);
        
        // Если время удалось распарсить И оно новее, чем время отсечения
        if (record_time > cutoff_time) {
            lines_to_keep.push_back(line);
        }
    }
    in.close();

    // Перезаписываем файл, оставляя только актуальные строки
    std::ofstream out(filename, std::ios::trunc);
    for (const auto& l : lines_to_keep) {
        out << l << "\n";
    }
    out.close();
}

#ifdef _WIN32
HANDLE open_and_config_port(const std::string& portName) {
    HANDLE hSerial = CreateFileA(
        portName.c_str(),
        GENERIC_READ,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hSerial == INVALID_HANDLE_VALUE) {
        std::cerr << "Error: Could not open port " << portName << std::endl;
        return INVALID_HANDLE_VALUE;
    }

    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    // Получаем текущие настройки
    if (!GetCommState(hSerial, &dcbSerialParams)) {
        std::cerr << "Error: GetCommState failed." << std::endl;
        CloseHandle(hSerial);
        return INVALID_HANDLE_VALUE;
    }

    // НАСТРАИВАЕМ СКОРОСТЬ
    dcbSerialParams.BaudRate = CBR_9600; // Стандартная скорость 9600
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    // Устанавливаем новые настройки
    if (!SetCommState(hSerial, &dcbSerialParams)) {
        std::cerr << "Error: SetCommState failed." << std::endl;
        CloseHandle(hSerial);
        return INVALID_HANDLE_VALUE;
    }

    // Настройка таймаутов (чтобы ReadFile не висел вечно)
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50; 
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    SetCommTimeouts(hSerial, &timeouts);

    return hSerial;
}
#endif

int main() {
    std::string portName;
    std::cout << "Enter Serial Port to READ (e.g. COM2 or /dev/pts/2): ";
    std::cin >> portName;

    std::vector<double> hour_buffer;
    std::vector<double> day_buffer;

    auto last_hour = std::chrono::system_clock::now();
    auto last_day = std::chrono::system_clock::now();

    // Для демонстрации можно уменьшить время (раскомментируй для быстрых тестов):
    // using ChronoHour = std::chrono::seconds; // "Час" длится 1 секунду
    // using ChronoMin  = std::chrono::seconds; // "Минута" тоже
    // В продакшене используй нормальные типы:
    using ChronoHour = std::chrono::hours;
    using ChronoMin  = std::chrono::minutes;

    while (true) {
        double current_temp = 0;
        bool read_success = false;
        
        // Чтение из порта
// Чтение из порта
#ifdef _WIN32
        HANDLE hSerial = open_and_config_port(portName); // <--- ИСПОЛЬЗУЕМ НОВУЮ ФУНКЦИЮ
        if (hSerial != INVALID_HANDLE_VALUE) {
            char buf[32] = {0};
            DWORD read;
            // ReadFile читает, пока не наберет 31 байт или не истечет таймаут
            if (ReadFile(hSerial, buf, 31, &read, NULL) && read > 0) {
                current_temp = std::atof(buf);
                read_success = true;
            }
            CloseHandle(hSerial);
        }
#else
        int fd = open(portName.c_str(), O_RDONLY | O_NOCTTY);
        if (fd != -1) {
            char buf[32] = {0};
            int n = read(fd, buf, 31);
            if (n > 0) {
                current_temp = std::atof(buf);
                read_success = true;
            }
            close(fd);
        }
#endif

        if (read_success && current_temp > 0) {
            std::string ts = get_timestamp();
            hour_buffer.push_back(current_temp);
            
            // 1. Лог всех измерений (Raw) -> храним 24 часа
            std::ofstream f1(LOG_RAW_24H, std::ios::app);
            f1 << ts << " " << current_temp << "\n";
            f1.close();

            std::cout << "[" << ts << "] Temp: " << current_temp << std::endl;
        }

        auto now = std::chrono::system_clock::now();

        // 2. Проверка часа (Среднее за час)
        // Если прошло 60 минут с последнего замера
        if (std::chrono::duration_cast<ChronoMin>(now - last_hour).count() >= 60) {
            if (!hour_buffer.empty()) {
                // Важно: 0.0, чтобы деление было вещественным
                double avg = std::accumulate(hour_buffer.begin(), hour_buffer.end(), 0.0) / hour_buffer.size();
                
                std::ofstream f2(LOG_HOURLY_MON, std::ios::app);
                f2 << get_timestamp() << " AVG_HOUR: " << avg << "\n";
                f2.close();
                
                day_buffer.push_back(avg);
                hour_buffer.clear();
            }
            last_hour = now;
            
            // Очистка RAW лога: оставляем только последние 24 часа
            rotate_log(LOG_RAW_24H, 24); 
        }

        // 3. Проверка дня (Среднее за день)
        // Если прошло 24 часа с последнего замера
        if (std::chrono::duration_cast<ChronoHour>(now - last_day).count() >= 24) {
            if (!day_buffer.empty()) {
                double avg = std::accumulate(day_buffer.begin(), day_buffer.end(), 0.0) / day_buffer.size();
                
                std::ofstream f3(LOG_DAILY_YEAR, std::ios::app);
                f3 << get_timestamp() << " AVG_DAY: " << avg << "\n";
                f3.close();
                
                day_buffer.clear();
            }
            last_day = now;

            // Очистка лога за месяц: 24 часа * 30 дней = 720 часов
            rotate_log(LOG_HOURLY_MON, 720); 
            
            // Очистка лога за год (опционально): 24 * 365 = 8760 часов
            rotate_log(LOG_DAILY_YEAR, 8760); 
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}