#include <iostream>
#include <string>
#include <memory>
#include <cstdlib>
#include "OCR_Logic.h"
#include "DocumentDescription.h"
#include "FieldExtractionService.h" // модуль с регулярными выражениями
#include "pythonConnect.h"
#include "Start.h"

using namespace std;


int main() {
    // Установка кодировки один раз для всей программы
    system("chcp 65001 >nul");

    int choice = 0;
    bool running = true;

    do {
        cout << "\n--- МЕНЮ ---" << endl;
        cout << "1. Добавить документ" << endl;
        cout << "0. Завершить программу" << endl;
        cout << "Введите выбор: ";

        cin >> choice;
        // Очистка буфера после cin, чтобы getline работал корректно
        cin.ignore(numeric_limits<streamsize>::max(), '\n');

        switch (choice) {
        case 1: {
            try {
                string filePath;
                cout << "Введите путь к файлу: ";
                getline(cin, filePath);

                if (filePath.empty()) {
                    throw runtime_error("Путь к файлу не может быть пустым");
                }

                // Строка подключения без пароля: аутентификация на стороне
                // PostgreSQL должна быть настроена в pg_hba.conf на метод "trust"
                // для данного хоста/пользователя, иначе сервер отклонит подключение.
                string connStr = "host=127.0.0.1 dbname=postgres user=postgres";

                DocumentDescriptionRepository repo(connStr);
                FieldExtractionService extractionService(connStr);

                // Порядок вызовов важен: сначала создаётся родительская
                // таблица documents, и только после неё — таблицы,
                // ссылающиеся на неё через внешний ключ (document_id).
                repo.createDocumentsTable();
                repo.createTable();
                extractionService.createTable();

                int targetId = repo.addEmptyDocument();
                repo.addPath(targetId, filePath);

                string resultTxt = ocrLogic(targetId, filePath, repo);
                string ocrText = readTextFile(resultTxt);
                ocrText = sanitizeUtf8(ocrText); // очищаем текст OCR от повреждённых байт перед дальнейшей обработкой

                auto extractedFields = extractionService.extractAndSave(targetId, ocrText);
                cout << "Регулярными выражениями найдено полей: " << extractedFields.size() << endl;

                string command = "python test.py " + resultTxt;
                string aiData = getPythonResult(command);

                if (!aiData.empty()) {
                    repo.create(targetId, ocrText, aiData, "автоматический анализ");
                    cout << "Успех! Данные записаны в БД." << endl;
                }
                else {
                    cerr << "ИИ ничего не вернул." << endl;
                }
            }
            catch (const exception& e) {
                cerr << "Ошибка при обработке документа: " << e.what() << endl;
            }
            break;
        }

        case 0:
            running = false;
            cout << "Завершение программы..." << endl;
            break;

        default:
            cout << "Неверный выбор, попробуйте снова." << endl;
            break;
        }
    } while (running);

    return 0;
}