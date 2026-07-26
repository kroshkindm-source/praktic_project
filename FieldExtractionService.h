#pragma once
#include <string>    // хранение текста (текст OCR, найденные значения)
#include <vector>    // списки найденных значений и результатов
#include <optional>  // (зарезервировано для методов, не гарантирующих результат)
#include <pqxx/pqxx> // подключение и запросы к PostgreSQL

// Одно найденное регуляркой значение: номер, дата или сумма.
// Хранится в БД как отдельная строка таблицы extracted_fields.
struct ExtractedField {
    int id = 0;              // id записи в БД
    int documentId = 0;      // id документа, к которому относится значение
    std::string fieldName;   // тип поля: "invoice_number" / "issue_date" / "amount"
    std::string fieldValue;  // само найденное значение
    double confidence = 0.0; // уверенность в результате (0..1)
};

// Извлекает регулярными выражениями ВСЕ номера, даты и суммы из текста
// документа и сохраняет каждое найденное значение в PostgreSQL.
class FieldExtractionService {
public:
    explicit FieldExtractionService(const std::string& connectionString);

    // Создаёт таблицу extracted_fields, если её ещё нет.
    void createTable();

    // Регистрирует документ в таблице documents, возвращает его id.
    int addDocument(int userId, const std::string& originalFilename, const std::string& filePath,
        const std::string& fileType, long long fileSize);

    // Прогоняет текст через все три regex-метода ниже и сохраняет
    // в БД каждое найденное значение. Возвращает список сохранённого.
    std::vector<ExtractedField> extractAndSave(int documentId, const std::string& ocrText);

    // Возвращает все поля, ранее найденные для документа.
    std::vector<ExtractedField> getByDocumentId(int documentId);

    // Удаляет запись по id, возвращает true, если она существовала.
    bool remove(int id);

private:
    pqxx::connection connection_; // открытое соединение с БД

    // Сохраняет одно значение в extracted_fields, возвращает id записи.
    int saveField(int documentId, const std::string& fieldName,
        const std::string& fieldValue, double confidence);

    // Regex-методы ниже используют wsregex_iterator/sregex_iterator —
    // находят ВСЕ непересекающиеся совпадения, а не только первое,
    // и отфильтровывают повторяющиеся точные значения.

    // Данный метод нужен для поиска номера
    std::vector<std::string> extractInvoiceNumbers(const std::string& text) const;

    // Данный метод нужен для поиска даты
    std::vector<std::string> extractDates(const std::string& text) const;

    // Данный метод нужен для поиска суммы
    std::vector<std::string> extractAmounts(const std::string& text) const;
};