#include "FieldExtractionService.h"
#include <pqxx/pqxx>
#include <regex>
#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>   // подключается ПОСЛЕ pqxx/pqxx во избежание конфликта winsock.h/winsock2.h

// ============================================================================
// Вспомогательные функции преобразования UTF-8 <-> UTF-16 (wchar_t).
// Требуются только для extractInvoiceNumber: диапазон [А-Я][а-я] корректно
// работает лишь при посимвольном (не побайтовом) разборе строки.
// ============================================================================
namespace {
    std::wstring utf8ToWide(const std::string& utf8) {
        if (utf8.empty()) return std::wstring();
        int wideLen = MultiByteToWideChar(
            CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
        if (wideLen <= 0) return std::wstring();

        std::wstring wide(static_cast<size_t>(wideLen), L'\0');
        MultiByteToWideChar(
            CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), wideLen);
        return wide;
    }

    std::string wideToUtf8(const std::wstring& wide) {
        if (wide.empty()) return std::string();
        int utf8Len = WideCharToMultiByte(
            CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
            nullptr, 0, nullptr, nullptr);
        if (utf8Len <= 0) return std::string();

        std::string utf8(static_cast<size_t>(utf8Len), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
            utf8.data(), utf8Len, nullptr, nullptr);
        return utf8;
    }
}


// Конструктор. Здесь происходит подключение к базе данных — один раз,
// при создании объекта FieldExtractionService.
FieldExtractionService::FieldExtractionService(const std::string& connectionString)
    : connection_(connectionString) {
}

// Создаёт таблицу extracted_fields, если она ещё не существует.
// IF NOT EXISTS означает: "если такая таблица уже есть — ничего не делать,
// ошибки не будет".
void FieldExtractionService::createTable() {
    pqxx::work txn(connection_); // открываем транзакцию — своего рода "черновик" изменений
    txn.exec(
        "CREATE TABLE IF NOT EXISTS extracted_fields ("
        "id SERIAL PRIMARY KEY,"  
                                       // номер записи, создаётся автоматически
        "document_id INT NOT NULL REFERENCES documents(id) ON DELETE CASCADE," // ссылка на документ; если документ удалят, поля удалятся тоже
        "field_name VARCHAR(50) NOT NULL,"                                // название поля ("invoice_number" и т.п.)
        "field_value TEXT NOT NULL,"                                      // само значение
        "confidence NUMERIC(5,2) NOT NULL DEFAULT 0,"                     // уверенность в найденном значении
        "extracted_at TIMESTAMP NOT NULL DEFAULT NOW()"                   // когда значение было найдено
        ");"
    );
    txn.commit(); // подтверждаем изменения — только теперь они реально применяются в БД
}

// Добавляет новую строку в таблицу documents — то есть "регистрирует"
// новый документ в базе данных — и возвращает id, который база данных
// присвоила этой строке. Этот id затем передаётся в extractAndSave(),
// чтобы все найденные регулярными выражениями поля были привязаны
// именно к этому документу через внешний ключ document_id.
//
// Таблицу documents этот метод не создаёт — она должна быть создана
// заранее (см. DocumentRepository::createTable() из файла Document.cpp).
int FieldExtractionService::addDocument(int userId, const std::string& originalFilename,
    const std::string& filePath, const std::string& fileType, long long fileSize) {
    pqxx::work txn(connection_);

    // RETURNING id — просим базу данных сразу вернуть номер, который
    // она присвоила новой строке, чтобы не делать отдельный запрос за ним.
    pqxx::result res = txn.exec(
        "INSERT INTO documents (user_id, original_filename, file_path, file_type, file_size) "
        "VALUES ($1, $2, $3, $4, $5) RETURNING id;",
        pqxx::params{ userId, originalFilename, filePath, fileType, fileSize }
    );

    txn.commit();
    return res[0][0].as<int>(); // id нового документа — именно то новое поле ID, с которым будут связаны найденные поля
}

// Сохраняет одно найденное поле в базу данных.
// Возвращает id, который база данных присвоила новой записи.
int FieldExtractionService::saveField(int documentId, const std::string& fieldName,
    const std::string& fieldValue, double confidence) {
    pqxx::work txn(connection_);

    // $1, $2, $3, $4 — это "заглушки" для значений. Они безопасно
    // подставляются библиотекой pqxx через pqxx::params, поэтому
    // текст (например, из OCR) не может случайно сломать SQL-запрос.
    pqxx::result res = txn.exec(
        "INSERT INTO extracted_fields (document_id, field_name, field_value, confidence) "
        "VALUES ($1, $2, $3, $4) RETURNING id;",
        pqxx::params{ documentId, fieldName, fieldValue, confidence }
    );

    txn.commit();
    return res[0][0].as<int>(); // достаём id только что созданной записи
}

// ============================================================================
// Ниже — три функции поиска. Все они работают по одному и тому же принципу:
// 1) задаём "шаблон" (регулярное выражение) — как выглядит нужный нам текст;
// 2) пытаемся найти в тексте кусок, подходящий под этот шаблон;
// 3) если нашли — возвращаем найденный текст, если нет — возвращаем "пусто".
//
// Шаблоны написаны с расчётом на то, что OCR распознаёт текст неидеально:
// - между словами и цифрами может быть разное количество пробелов,
//   поэтому вместо одного пробела используется \s* (ноль или более пробелов);
// - дефис, точка и слэш в датах могут перепутаться, поэтому используется
//   один общий класс символов [.\-\/] вместо одного конкретного символа.
// ============================================================================

std::optional<std::string> FieldExtractionService::extractInvoiceNumber(const std::string& text) const {
    // std::wregex: один элемент строки = один символ (code unit UTF-16),
    // поэтому диапазоны [А-Я] и [а-я] интерпретируются как диапазон букв,
    // а не диапазон байт — совпадение никогда не "разрезает" символ пополам.
    static const std::wregex pattern(
        LR"([N№]\s*[:\-]?\s*([A-Za-zА-Яа-я0-9][A-Za-zА-Яа-я0-9\-\/]{2,20}))",
        std::regex::icase
    );

    std::wstring wideText = utf8ToWide(text); // UTF-8 -> UTF-16 перед матчингом

    std::wsmatch match;
    if (std::regex_search(wideText, match, pattern) && match.size() > 1) {
        return wideToUtf8(match[1].str()); // обратно в UTF-8 для записи в PostgreSQL
    }
    return std::nullopt;
}

std::optional<std::string> FieldExtractionService::extractDate(const std::string& text) const {
    // Без изменений: шаблон использует только ASCII (цифры, '.', '-', '/').
    // Такие байты в UTF-8 никогда не входят в состав многобайтовых
    // последовательностей, поэтому побайтовый std::regex работает корректно.
    static const std::regex pattern(
        R"((\d{1,2})\s*[.\-\/]\s*(\d{1,2})\s*[.\-\/]\s*(\d{2,4}))"
    );

    std::smatch match;
    if (std::regex_search(text, match, pattern)) {
        return match[1].str() + "." + match[2].str() + "." + match[3].str();
    }
    return std::nullopt;
}

std::optional<std::string> FieldExtractionService::extractAmount(const std::string& text) const {
    // Без изменений: захватываемая группа состоит только из ASCII (цифры,
    // пробел, '.', ','); литералы "руб"/"₽" сравниваются как точная
    // последовательность байт, а не как символьный диапазон, поэтому
    // побайтовое сравнение корректно при условии, что сам исходный файл
    // сохранён в UTF-8 с BOM и компилируется с флагом /utf-8.
    static const std::regex pattern(
        R"((\d[\d\s]*[.,]?\d*)\s*(?:руб\.?|₽))",
        std::regex::icase
    );

    std::smatch match;
    if (std::regex_search(text, match, pattern) && match.size() > 1) {
        return match[1].str();
    }
    return std::nullopt;
}


// ============================================================================
// Главный метод: принимает текст документа и id самого документа,
// прогоняет текст через все три функции поиска выше, и то, что удалось
// найти, сразу сохраняет в базу данных.
// ============================================================================
std::vector<ExtractedField> FieldExtractionService::extractAndSave(int documentId, const std::string& ocrText) {
    std::vector<ExtractedField> savedFields; // сюда будем складывать то, что реально сохранили в БД

    // Небольшая вспомогательная структура: название поля + результат поиска.
    // Это нужно, чтобы не повторять один и тот же код три раза подряд —
    // ниже мы просто проходим по списку правил в цикле.
    struct FieldRule {
        std::string name;
        std::optional<std::string> value;
    };

    std::vector<FieldRule> rules{
        { "invoice_number", extractInvoiceNumber(ocrText) },
        { "issue_date",     extractDate(ocrText) },
        { "amount",         extractAmount(ocrText) }
    };

    // Проходим по каждому правилу: если значение найдено — сохраняем его в БД.
    for (const auto& rule : rules) {
        if (!rule.value.has_value()) {
            continue; // это поле не нашлось в тексте — просто пропускаем его
        }

        const double confidence = 1.0; // упрощённо считаем, что раз шаблон совпал — уверенность максимальная

        // Сохраняем найденное значение в базу данных и получаем его id.
        const int newId = saveField(documentId, rule.name, rule.value.value(), confidence);

        // Формируем объект для возврата — чтобы вызывающий код сразу видел,
        // что именно было сохранено, без отдельного запроса к БД.
        ExtractedField field;
        field.id = newId;
        field.documentId = documentId;
        field.fieldName = rule.name;
        field.fieldValue = rule.value.value();
        field.confidence = confidence;
        savedFields.push_back(field);
    }

    return savedFields;
}

// Вспомогательная функция, видимая только внутри этого файла (анонимное
// пространство имён). Превращает одну строку результата SQL-запроса
// в удобный объект ExtractedField.
namespace {
    // Принимаем по значению или константной ссылке, 
    // но убедитесь, что тип — именно pqxx::row
    ExtractedField rowToField(pqxx::row row) { 
        ExtractedField f;
        f.id = row["id"].as<int>();
        f.documentId = row["document_id"].as<int>();
        f.fieldName = row["field_name"].as<std::string>();
        f.fieldValue = row["field_value"].as<std::string>();
        f.confidence = row["confidence"].as<double>();
        return f;
    }
}

// Возвращает все поля, ранее найденные и сохранённые для указанного документа.
std::vector<ExtractedField> FieldExtractionService::getByDocumentId(int documentId) {
    pqxx::work txn(connection_);
    pqxx::result res = txn.exec(
        "SELECT * FROM extracted_fields WHERE document_id = $1 ORDER BY id;",
        pqxx::params{ documentId }
    );

    std::vector<ExtractedField> fields;
    fields.reserve(res.size()); // заранее выделяем память под нужное число элементов
    for (auto row : res) {
        fields.push_back(rowToField(pqxx::row(row))); // превращаем каждую строку результата в объект ExtractedField
    }
    return fields;
}

// Удаляет одну запись из таблицы extracted_fields по её id.
bool FieldExtractionService::remove(int id) {
    pqxx::work txn(connection_);
    pqxx::result res = txn.exec("DELETE FROM extracted_fields WHERE id = $1;", pqxx::params{ id });
    txn.commit();
    // affected_rows() — сколько строк реально было удалено.
    // Если > 0, значит запись с таким id существовала и была удалена.
    return res.affected_rows() > 0;
}
