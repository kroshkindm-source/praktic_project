#pragma once
#include <string>
#include <vector>
#include <optional>
#include <pqxx/pqxx>

// Это описание ОДНОЙ найденной "полезной информации" из текста документа.
// Например: номер счёта, дата, сумма. Каждое такое найденное значение
// хранится в базе данных как отдельная строка таблицы extracted_fields.
struct ExtractedField {
    int id = 0;              // номер записи в базе данных (создаётся автоматически)
    int documentId = 0;      // к какому документу относится это значение
    std::string fieldName;   // название поля, например "invoice_number"
    std::string fieldValue;  // само найденное значение, например "INV-2024-0001"
    double confidence = 0.0; // насколько мы уверены, что нашли значение правильно (0..1)
};

// Этот класс делает две вещи:
// 1) ищет в тексте (полученном от OCR) нужные данные при помощи
//    регулярных выражений (номер счёта, дату, сумму);
// 2) сохраняет найденные данные в базу данных PostgreSQL.
//
// Соединение с базой данных хранится прямо внутри класса (поле connection_)
// и открывается один раз — при создании объекта. Так не приходится
// каждый раз заново подключаться к базе при каждом вызове метода.
class FieldExtractionService {
public:
    // При создании объекта нужно передать строку подключения к БД, например:
    // "dbname=ocr_db user=postgres password=postgres host=localhost port=5432"
    explicit FieldExtractionService(const std::string& connectionString);

    // Создаёт таблицу extracted_fields в базе данных, если её ещё нет.
    // Вызывать один раз при первом запуске программы.
    void createTable();

    // Добавляет новый документ в таблицу documents и возвращает его id.
    // Этот id дальше используется, чтобы "привязать" к документу все
    // найденные регулярными выражениями поля (передаётся в extractAndSave).
    // Таблица documents должна уже существовать в базе данных (создаётся
    // отдельно, через DocumentRepository::createTable()).
    int addDocument(int userId, const std::string& originalFilename, const std::string& filePath,
        const std::string& fileType, long long fileSize);

    // Главный метод класса. Принимает:
    // - documentId — id документа, к которому относится текст;
    // - ocrText — сам текст, распознанный OCR.
    // Метод ищет в тексте номер счёта, дату и сумму, сохраняет то, что нашёл,
    // в базу данных и возвращает список того, что удалось сохранить.
    std::vector<ExtractedField> extractAndSave(int documentId, const std::string& ocrText);

    // Возвращает все поля, которые ранее были найдены и сохранены
    // для конкретного документа.
    std::vector<ExtractedField> getByDocumentId(int documentId);

    // Удаляет одну запись из таблицы extracted_fields по её id.
    // Возвращает true, если запись действительно была удалена.
    bool remove(int id);

private:
    pqxx::connection connection_; // соединение с базой данных, открыто на весь срок жизни объекта

    // Служебный метод: сохраняет ОДНО найденное поле в базу данных
    // и возвращает id новой записи. Используется внутри extractAndSave.
    int saveField(int documentId, const std::string& fieldName,
        const std::string& fieldValue, double confidence);

    // Ниже — три метода поиска. Каждый ищет в тексте своё конкретное значение.
    // Если значение не найдено, возвращается std::nullopt (то есть "пусто").

    // Ищет номер счёта/документа, например: "№ INV-2024-0001"
    std::optional<std::string> extractInvoiceNumber(const std::string& text) const;

    // Ищет дату в формате день.месяц.год, например: "15.03.2024"
    std::optional<std::string> extractDate(const std::string& text) const;

    // Ищет денежную сумму перед словом "руб" или знаком "₽", например: "15000 руб."
    std::optional<std::string> extractAmount(const std::string& text) const;
};
