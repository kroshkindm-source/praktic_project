#include <iostream>
#include <string>
#include <memory>
#include <cstdlib>
#include <limits> // Обязательно для numeric_limits

#include "OCR_Logic.h"
#include "DocumentDescription.h"
#include "FieldExtractionService.h"
#include "pythonConnect.h"
#include "Start.h"

using namespace std;

// Инициализируем БД один раз при запуске программы
void initializeDatabase(const string& connStr) {
    DocumentDescriptionRepository repo(connStr);
    FieldExtractionService extractionService(connStr);
    
    repo.createDocumentsTable();
    repo.createTable();
    extractionService.createTable();
}

int main() {
    // Установка кодировки
    system("chcp 65001 >nul");

    const string connStr = "host=127.0.0.1 dbname=postgres user=postgres";
    
    try {
        initializeDatabase(connStr);
    } catch (const exception& e) {
        cerr << "Критическая ошибка инициализации БД: " << e.what() << endl;
        return 1;
    }

    int choice = 0;
    bool running = true;

    do {
        cout << "\n--- МЕНЮ ---" << endl;
        cout << "1. Добавить документ" << endl;
        cout << "0. Завершить программу" << endl;
        cout << "Введите выбор: ";

        if (!(cin >> choice)) {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            continue;
        }
        cin.ignore(numeric_limits<streamsize>::max(), '\n');

        switch (choice) {
        case 1: {
            string filePath;
            cout << "Введите путь к файлу: ";
            getline(cin, filePath);

            if (filePath.empty()) {
                cerr << "Ошибка: Путь к файлу не может быть пустым" << endl;
                break;
            }

            try {
                DocumentDescriptionRepository repo(connStr);
                FieldExtractionService extractionService(connStr);

                int targetId = repo.addEmptyDocument();
                repo.addPath(targetId, filePath);

                // OCR обработка
                string resultTxt = ocrLogic(targetId, filePath, repo);
                string ocrText = sanitizeUtf8(readTextFile(resultTxt));

                // Регулярные выражения
                auto extractedFields = extractionService.extractAndSave(targetId, ocrText);
                cout << "Регулярными выражениями найдено полей: " << extractedFields.size() << endl;

                // Вызов Python
                string command = "python test.py \"" + filePath + "\"";
                string aiData = getPythonResult(command);

                if (!aiData.empty()) {
                    repo.update(targetId, ocrText, aiData, "автоматический анализ"); 
                    cout << "Успех! Данные обновлены в БД." << endl;
                } else {
                    cerr << "Предупреждение: ИИ не вернул данных." << endl;
                }
            }
            catch (const exception& e) {
                cerr << "Ошибка при обработке документа: " << e.what() << endl;
            }
            break;
        }

        case 0:
            running = false;
            break;

        default:
            cout << "Неверный выбор." << endl;
            break;
        }
    } while (running);

    return 0;
}