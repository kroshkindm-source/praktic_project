#include <iostream>
#include <string>
#include <cstdio>
#include <memory>
#include <cstdlib>
#include <stdexcept>
#include <fstream>   // нужен, чтобы прочитать текстовый файл, который создал OCR
#include <sstream>   // нужен, чтобы собрать содержимое файла в одну строку
#include "OCR_Logic.h"
#include "DocumentDescription.h"
#include "FieldExtractionService.h" // модуль с регулярными выражениями
#include "pythonConnect.h"

using namespace std;

// Читает весь текстовый файл в одну строку
string readTextFile(const string& path) {
    ifstream file(path);    
    if (!file.is_open()) {
        throw runtime_error("Не удалось открыть файл с результатом OCR: " + path);
    }
    stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main() {
    // Установка кодировки один раз для всей программы
    system("chcp 65001 >nul");

    int choice;
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

                    // Твоя логика работы с БД и OCR
                    DocumentDescriptionRepository repo("host=127.0.0.1 dbname=postgres user=postgres");
                    FieldExtractionService extractionService("host=127.0.0.1 dbname=postgres user=postgres");
                    extractionService.createTable();

                    int targetId = repo.addEmptyDocument();
                    repo.addPath(targetId, filePath);
                    
                    string resultTxt = ocrLogic(targetId, filePath, repo);
                    string ocrText = readTextFile(resultTxt);

                    auto extractedFields = extractionService.extractAndSave(targetId, ocrText);
                    cout << "Регулярными выражениями найдено полей: " << extractedFields.size() << endl;

                    string command = "python test.py " + resultTxt;
                    string aiData = getPythonResult(command);

                    if (!aiData.empty()) {
                        repo.create(targetId, aiData, "автоматический анализ");
                        cout << "Успех! Данные записаны в БД." << endl;
                    } else {
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
