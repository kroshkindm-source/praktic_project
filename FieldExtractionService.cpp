#include "FieldExtractionService.h"
#include <pqxx/pqxx>
#include <regex>
#include <string>

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
    // Шаблон ищет: букву N или знак № (без учёта регистра),
    // затем, возможно, двоеточие или дефис, затем сам номер счёта.
    // Пример подходящего текста: "№ INV-2024-0001", "N: A1-2024"
    static const std::regex pattern(
        R"([N№]\s*[:\-]?\s*([A-Za-zА-Яа-я0-9][A-Za-zА-Яа-я0-9\-\/]{2,20}))",
        std::regex::icase // не учитывать регистр букв (N и n — одно и то же)
    );

    std::smatch match; // сюда попадёт результат поиска, если он будет найден
    if (std::regex_search(text, match, pattern) && match.size() > 1) {
        return match[1].str(); // возвращаем найденный номер (без слова "№")
    }
    return std::nullopt; // ничего не нашли
}

std::optional<std::string> FieldExtractionService::extractDate(const std::string& text) const {
    // Шаблон ищет три группы цифр, разделённые точкой, дефисом или слэшем:
    // день (1-2 цифры), месяц (1-2 цифры), год (2-4 цифры).
    // Пример подходящего текста: "15.03.2024", "15-03-24", "15/03/2024"
    static const std::regex pattern(
        R"((\d{1,2})\s*[.\-\/]\s*(\d{1,2})\s*[.\-\/]\s*(\d{2,4}))"
    );

    std::smatch match;
    if (std::regex_search(text, match, pattern)) {
        // Собираем найденную дату обратно в привычном формате "день.месяц.год"
        return match[1].str() + "." + match[2].str() + "." + match[3].str();
    }
    return std::nullopt;
}

std::optional<std::string> FieldExtractionService::extractAmount(const std::string& text) const {
    // Шаблон ищет число (с возможными пробелами внутри и дробной частью
    // через точку или запятую), после которого идёт слово "руб" или знак "₽".
    // Пример подходящего текста: "15000 руб.", "1 500,50 руб", "2300 ₽"
    static const std::regex pattern(
        R"((\d[\d\s]*[.,]?\d*)\s*(?:руб\.?|₽))",
        std::regex::icase
    );

    std::smatch match;
    if (std::regex_search(text, match, pattern) && match.size() > 1) {
        return match[1].str(); // возвращаем только число, без слова "руб"
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
