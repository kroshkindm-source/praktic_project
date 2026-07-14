#include <array>
#include <memory>
#include <iostream>
#include "pythonConnect.h"
// Функция для чтения вывода Python
std::string getPythonResult(const std::string& cmd) {
    std::array<char, 4096> buffer;
    std::string result;
    // pclose закроет поток и вернет статус выполнения
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(cmd.c_str(), "r"), _pclose);
    if (!pipe) return "";
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}