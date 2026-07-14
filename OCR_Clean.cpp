//C:\msys64\ucrt64\bin\postgres.exe -D "C:\pgsql_data"

#include "OCR_Logic.h"
#include "DocumentDescription.h"
#include <stdexcept>

using namespace std;

string ocrLogic(int docId, const string& imagePath, DocumentDescriptionRepository& repo) {
    auto docOpt = repo.getByDocumentId(docId);
    if (!docOpt.has_value()) {
        throw runtime_error("Document with ID: " + to_string(docId) + " not found in database");
    }

    string idStr = to_string(docId);
    string outputName = "doc_" + idStr;
    
    // 1. Укажите путь к исполняемому файлу tesseract.exe
    string tesseractPath = "C:\\Program Files\\Tesseract-OCR\\tesseract.exe";
    
    // 2. Укажите путь к папке tessdata
    string tessDataDir = "C:\\Program Files\\Tesseract-OCR\\tessdata";

    // 3. Формируем правильную команду:
    // tesseract [входной_файл] [выходной_файл] --tessdata-dir [путь]
    // Используем кавычки вокруг путей на случай, если в них есть пробелы
    // Обратите внимание на дополнительные кавычки в начале и конце
    // 3. Формируем команду с добавлением параметра -l rus
    // Мы добавляем " -l rus+eng" после имени выходного файла
    string command = "cmd /c \"\"" + tesseractPath + "\" \"" + imagePath + "\" \"" + outputName + "\" -l rus+eng --tessdata-dir \"" + tessDataDir + "\"\"";

    int exitcode = system(command.c_str());
    if (exitcode != 0) {
        throw runtime_error("Tesseract error for ID: " + idStr + " (Exit code: " + to_string(exitcode) + ")");
    }

    return outputName + ".txt";
}