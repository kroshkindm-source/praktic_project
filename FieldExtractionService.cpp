#include "FieldExtractionService.h"
#include <pqxx/pqxx>
#include <regex>
#include <string>
#include <algorithm>
#include <sstream>

FieldExtractionService::FieldExtractionService(const std::string& connectionString)
    : connection_(connectionString) {
}

void FieldExtractionService::createTable() {
    pqxx::work txn(connection_);
    txn.exec(
        "CREATE TABLE IF NOT EXISTS extracted_fields ("
        "id SERIAL PRIMARY KEY,"
        "document_id INT NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
        "field_name VARCHAR(50) NOT NULL,"
        "field_value TEXT NOT NULL,"
        "confidence NUMERIC(5,2) NOT NULL DEFAULT 0,"
        "extracted_at TIMESTAMP NOT NULL DEFAULT NOW());"
    );
    txn.commit();
}

// Убирает повторы, сохраняя порядок
namespace {
    std::vector<std::string> deduplicate(std::vector<std::string> values) {
        std::vector<std::string> unique;
        for (auto& v : values) {
            if (std::find(unique.begin(), unique.end(), v) == unique.end())
                unique.push_back(std::move(v));
        }
        return unique;
    }
}

// Ищет номера после №, N, Номер, номер, No
std::vector<std::string> FieldExtractionService::extractInvoiceNumbers(const std::string& text) const {
    static const std::regex pattern(
        R"((?:[N№]|Номер|номер|No|no)\s*[:\-]?\s*([A-Za-zА-Яа-я0-9][A-Za-zА-Яа-я0-9\-\/]{3,30}))",
        std::regex::icase
    );
    std::vector<std::string> results;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern); it != std::sregex_iterator(); ++it) {
        std::string val = (*it)[1].str();
        // Фильтруем мусор: должно быть хотя бы 4 символа и содержать цифру или дефис
        if (val.length() >= 4) {
            bool hasDigit = std::any_of(val.begin(), val.end(), ::isdigit);
            if (hasDigit) results.push_back(val);
        }
    }
    return deduplicate(std::move(results));
}

// Ищет даты в форматах DD.MM.YYYY, DD-MM-YYYY, DD/MM/YYYY
std::vector<std::string> FieldExtractionService::extractDates(const std::string& text) const {
    static const std::regex pattern(R"((\d{1,2})\s*[.\-\/]\s*(\d{1,2})\s*[.\-\/]\s*(\d{2,4}))");
    std::vector<std::string> results;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern); it != std::sregex_iterator(); ++it) {
        std::string day = (*it)[1].str();
        std::string month = (*it)[2].str();
        std::string year = (*it)[3].str();
        if (day.size() == 1) day = "0" + day;
        if (month.size() == 1) month = "0" + month;
        if (year.size() == 2) year = "20" + year;
        results.push_back(day + "." + month + "." + year);
    }
    return deduplicate(std::move(results));
}

// Ищет суммы перед "руб" или "₽"
std::vector<std::string> FieldExtractionService::extractAmounts(const std::string& text) const {
    static const std::regex pattern(
        R"((\d[\d\s]*[.,]?\d*)\s*(?:\([^)]*\)\s*)?(?:руб\.?|₽))",
        std::regex::icase
    );
    std::vector<std::string> results;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern); it != std::sregex_iterator(); ++it) {
        if (it->size() > 1) {
            std::string val = (*it)[1].str();
            // Убираем пробелы внутри числа
            val.erase(std::remove(val.begin(), val.end(), ' '), val.end());
            results.push_back(val);
        }
    }
    return deduplicate(std::move(results));
}

// Главный метод: ищет поля и обновляет regex_description в document_descriptions
std::string FieldExtractionService::extractAndSave(int documentId, const std::string& ocrText) {
    auto numbers = extractInvoiceNumbers(ocrText);
    auto dates = extractDates(ocrText);
    auto amounts = extractAmounts(ocrText);

    std::ostringstream result;
    
    if (!numbers.empty()) {
        result << "invoice_number: ";
        for (size_t i = 0; i < numbers.size(); ++i) {
            if (i > 0) result << ", ";
            result << numbers[i];
        }
        result << "\n";
    }
    
    if (!dates.empty()) {
        result << "issue_date: ";
        for (size_t i = 0; i < dates.size(); ++i) {
            if (i > 0) result << ", ";
            result << dates[i];
        }
        result << "\n";
    }
    
    if (!amounts.empty()) {
        result << "amount: ";
        for (size_t i = 0; i < amounts.size(); ++i) {
            if (i > 0) result << ", ";
            result << amounts[i];
        }
        result << "\n";
    }

    std::string regexResult = result.str();
    if (regexResult.empty()) {
        regexResult = "поля не найдены";
    }

    // Сохраняем в document_descriptions
    pqxx::work txn(connection_);
    txn.exec(
        "UPDATE document_descriptions SET regex_description = $1 WHERE document_id = $2;",
        pqxx::params{ regexResult, documentId }
    );
    txn.commit();

    return regexResult;
}