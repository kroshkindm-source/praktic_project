//PS C:\WINDOWS\system32> chcp 65001
//Active code page: 65001
//PS C:\WINDOWS\system32> psql -h 127.0.0.1 -U postgres -d postgres
//psql (18.4)
//ПРЕДУПРЕЖДЕНИЕ: Кодовая страница консоли (65001) отличается от основной
//                страницы Windows (1251).
//                8-битовые (русские) символы могут отображаться некорректно.
//                Подробнее об этом смотрите документацию psql, раздел
//                "Notes for Windows users".
//Введите "help", чтобы получить справку.

//postgres=# \encoding UTF8

#include <array>
#include <memory>
#include <iostream>
#include "pythonConnect.h"
using namespace std;
// Функция для чтения вывода Python
string getPythonResult(const string& command) {
    char buffer[128];
    string result = "";
    // Используем _popen для запуска команды
    FILE* pipe = _popen(command.c_str(), "r");
    if (!pipe) return "";

    // Читаем вывод в цикле
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
    }
    _pclose(pipe);
    return result;
}