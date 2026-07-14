#include "Start.h"
#include <fstream>
using namespace std;

// Читает весь текстовый файл в одну строку
string readTextFile(const string& path) {
    // Открываем как binary, чтобы не было никаких трансформаций символов перевода строки
    ifstream file(path, std::ios::binary); 
    if (!file.is_open()) {
        throw runtime_error("Не удалось открыть файл: " + path);
    }
    stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Проверяет строку на корректность кодировки UTF-8 и очищает её.
// Tesseract при распознавании низкокачественных изображений иногда
// выдаёт повреждённые байтовые последовательности (не образующие
// ни одного корректного UTF-8 символа). PostgreSQL с кодировкой
// клиента UTF8 отклоняет такие данные целиком при попытке INSERT.
// Каждый некорректный байт заменяется символом '?'; корректные байты
// (в т.ч. кириллица) копируются без изменений.
string sanitizeUtf8(const string& input) {
    string result;
    result.reserve(input.size());

    size_t i = 0;
    const size_t len = input.size();

    while (i < len) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        int extraBytes; // сколько дополнительных байт-продолжений ожидается

        if ((c & 0x80) == 0x00) {
            extraBytes = 0; // 0xxxxxxx — обычный однобайтовый символ (ASCII)
        }
        else if ((c & 0xE0) == 0xC0) {
            extraBytes = 1; // 110xxxxx — начало 2-байтового символа
        }
        else if ((c & 0xF0) == 0xE0) {
            extraBytes = 2; // 1110xxxx — начало 3-байтового символа (в т.ч. кириллица)
        }
        else if ((c & 0xF8) == 0xF0) {
            extraBytes = 3; // 11110xxx — начало 4-байтового символа
        }
        else {
            // Байт не может быть началом ни одной корректной UTF-8 последовательности
            result += '?';
            ++i;
            continue;
        }

        // Проверяем, что все ожидаемые байты-продолжения (10xxxxxx) присутствуют
        bool valid = (i + extraBytes < len);
        if (valid) {
            for (int j = 1; j <= extraBytes; ++j) {
                unsigned char cc = static_cast<unsigned char>(input[i + j]);
                if ((cc & 0xC0) != 0x80) {
                    valid = false;
                    break;
                }
            }
        }

        if (valid) {
            result.append(input, i, extraBytes + 1); // символ корректен — копируем целиком
            i += extraBytes + 1;
        }
        else {
            result += '?'; // символ повреждён — заменяем только стартовый байт
            ++i;
        }
    }

    return result;
}