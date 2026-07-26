#pragma once
#include <string>
#include <vector>
#include <optional>
#include <pqxx/pqxx>

// Одно найденное регуляркой значение: номер, дата или сумма.
struct ExtractedField {
    int id = 0;
    int documentId = 0;
    std::string fieldName;
    std::string fieldValue;
    double confidence = 0.0;
};

// Извлекает регулярными выражениями номера, даты и суммы из текста
// и сохраняет результат в document_descriptions.regex_description.
class FieldExtractionService {
public:
    explicit FieldExtractionService(const std::string& connectionString);

    // Создаёт таблицу extracted_fields, если её ещё нет (для совместимости).
    void createTable();

    // Прогоняет текст через регулярки и обновляет regex_description в document_descriptions.
    // Возвращает форматированную строку с результатами.
    std::string extractAndSave(int documentId, const std::string& ocrText);

private:
    pqxx::connection connection_;

    std::vector<std::string> extractInvoiceNumbers(const std::string& text) const;
    std::vector<std::string> extractDates(const std::string& text) const;
    std::vector<std::string> extractAmounts(const std::string& text) const;
};