#include "FieldExtractionService.h"
#include <pqxx/pqxx>
#include <regex>
#include <string>
#include <algorithm>
#include <cwctype>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h> // после pqxx/pqxx — иначе конфликт winsock.h/winsock2.h

namespace {
    // UTF-8 -> UTF-16 (нужно для extractInvoiceNumbers: диапазоны А-Я работают только посимвольно)
    std::wstring utf8ToWide(const std::string& utf8) {
        if (utf8.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
        if (len <= 0) return {};
        std::wstring wide(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), wide.data(), len);
        return wide;
    }

    // UTF-16 -> UTF-8 (для сохранения результата в PostgreSQL)
    std::string wideToUtf8(const std::wstring& wide) {
        if (wide.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string utf8(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), utf8.data(), len, nullptr, nullptr);
        return utf8;
    }

    // Убирает повторы, сохраняя порядок первого появления
    std::vector<std::string> deduplicate(std::vector<std::string> values) {
        std::vector<std::string> unique;
        for (auto& v : values)
            if (std::find(unique.begin(), unique.end(), v) == unique.end())
                unique.push_back(std::move(v));
        return unique;
    }
}

FieldExtractionService::FieldExtractionService(const std::string& connectionString)
    : connection_(connectionString) {
}

// Создаёт таблицу extracted_fields, если её ещё нет
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

// Регистрирует документ в таблице documents, возвращает его id
int FieldExtractionService::addDocument(int userId, const std::string& originalFilename,
    const std::string& filePath, const std::string& fileType, long long fileSize) {
    pqxx::work txn(connection_);
    auto res = txn.exec(
        "INSERT INTO documents (user_id, original_filename, file_path, file_type, file_size) "
        "VALUES ($1, $2, $3, $4, $5) RETURNING id;",
        pqxx::params{ userId, originalFilename, filePath, fileType, fileSize }
    );
    txn.commit();
    return res[0][0].as<int>();
}

// Сохраняет одно найденное значение, возвращает id новой записи
int FieldExtractionService::saveField(int documentId, const std::string& fieldName,
    const std::string& fieldValue, double confidence) {
    pqxx::work txn(connection_);
    auto res = txn.exec(
        "INSERT INTO extracted_fields (document_id, field_name, field_value, confidence) "
        "VALUES ($1, $2, $3, $4) RETURNING id;",
        pqxx::params{ documentId, fieldName, fieldValue, confidence }
    );
    txn.commit();
    return res[0][0].as<int>();
}

// Находит все номера (после "№"/"N"), содержащие хотя бы одну цифру
std::vector<std::string> FieldExtractionService::extractInvoiceNumbers(const std::string& text) const {
    static const std::wregex pattern(
        LR"([N№]\s*[:\-]?\s*((?=[A-Za-zА-Яа-я0-9\-\/]*\d)[A-Za-zА-Яа-я0-9][A-Za-zА-Яа-я0-9\-\/]{2,20}))",
        std::regex::icase
    );
    std::wstring wideText = utf8ToWide(text);
    std::vector<std::string> results;
    for (auto it = std::wsregex_iterator(wideText.begin(), wideText.end(), pattern); it != std::wsregex_iterator(); ++it)
        if (it->size() > 1) results.push_back(wideToUtf8((*it)[1].str()));
    return deduplicate(std::move(results));
}

// Находит все даты: сначала "от «DD» месяц YYYY", затем числовые DD.MM.YYYY
std::vector<std::string> FieldExtractionService::extractDates(const std::string& text) const {
    static const std::vector<std::pair<std::wstring, std::wstring>> months = {
        { L"январ", L"01" }, { L"феврал", L"02" }, { L"март", L"03" }, { L"апрел", L"04" },
        { L"ма", L"05" }, { L"июн", L"06" }, { L"июл", L"07" }, { L"август", L"08" },
        { L"сентябр", L"09" }, { L"октябр", L"10" }, { L"ноябр", L"11" }, { L"декабр", L"12" }
    };
    std::wstring wideText = utf8ToWide(text);
    std::vector<std::string> results;

    static const std::wregex headerPattern(
        LR"(от\s*[«"]?\s*(\d{1,2})\s*[»"]?\s+([А-Яа-я]+)\s+(\d{4}))", std::regex::icase);
    for (auto it = std::wsregex_iterator(wideText.begin(), wideText.end(), headerPattern); it != std::wsregex_iterator(); ++it) {
        std::wstring monthWord = (*it)[2].str();
        for (auto& ch : monthWord) ch = static_cast<wchar_t>(std::towlower(ch));
        for (const auto& [stem, number] : months) {
            if (monthWord.starts_with(stem)) {
                std::string day = wideToUtf8((*it)[1].str());
                if (day.size() == 1) day = "0" + day;
                results.push_back(day + "." + wideToUtf8(number) + "." + wideToUtf8((*it)[3].str()));
                break;
            }
        }
    }

    static const std::regex pattern(R"((\d{1,2})\s*[.\-\/]\s*(\d{1,2})\s*[.\-\/]\s*(\d{2,4}))");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern); it != std::sregex_iterator(); ++it)
        results.push_back((*it)[1].str() + "." + (*it)[2].str() + "." + (*it)[3].str());

    return deduplicate(std::move(results));
}

// Находит все суммы перед "руб"/"₽", допуская вставку вида "(один миллион)"
std::vector<std::string> FieldExtractionService::extractAmounts(const std::string& text) const {
    static const std::regex pattern(
        R"((\d[\d\s]*[.,]?\d*)\s*(?:\([^)]*\)\s*)?(?:руб\.?|₽))", std::regex::icase);
    std::vector<std::string> results;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern); it != std::sregex_iterator(); ++it)
        if (it->size() > 1) results.push_back((*it)[1].str());
    return deduplicate(std::move(results));
}

// Прогоняет текст через все три метода поиска, сохраняет каждое найденное значение в БД
std::vector<ExtractedField> FieldExtractionService::extractAndSave(int documentId, const std::string& ocrText) {
    std::vector<ExtractedField> saved;
    struct Rule { std::string name; std::vector<std::string> values; };
    std::vector<Rule> rules{
        { "invoice_number", extractInvoiceNumbers(ocrText) },
        { "issue_date",     extractDates(ocrText) },
        { "amount",         extractAmounts(ocrText) }
    };

    for (const auto& rule : rules)
        for (const auto& value : rule.values) {
            int id = saveField(documentId, rule.name, value, 1.0);
            saved.push_back({ id, documentId, rule.name, value, 1.0 });
        }
    return saved;
}

// Возвращает все поля, сохранённые для документа
std::vector<ExtractedField> FieldExtractionService::getByDocumentId(int documentId) {
    pqxx::work txn(connection_);
    auto res = txn.exec("SELECT * FROM extracted_fields WHERE document_id = $1 ORDER BY id;",
        pqxx::params{ documentId });

    std::vector<ExtractedField> fields;
    fields.reserve(res.size());
    for (auto row : res)
        fields.push_back({ row["id"].as<int>(), row["document_id"].as<int>(),
            row["field_name"].as<std::string>(), row["field_value"].as<std::string>(),
            row["confidence"].as<double>() });
    return fields;
}

// Удаляет запись по id, возвращает true, если запись существовала
bool FieldExtractionService::remove(int id) {
    pqxx::work txn(connection_);
    auto res = txn.exec("DELETE FROM extracted_fields WHERE id = $1;", pqxx::params{ id });
    txn.commit();
    return res.affected_rows() > 0;
}