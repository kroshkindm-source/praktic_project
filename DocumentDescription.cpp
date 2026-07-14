#include "DocumentDescription.h"
#include <pqxx/pqxx>
#include <string>

// Конструктор. Здесь происходит подключение к базе данных — один раз,
// при создании объекта DocumentDescriptionRepository.
DocumentDescriptionRepository::DocumentDescriptionRepository(const std::string& connectionString)
    : connection_(connectionString) {
}

// Создаёт таблицу documents, если она ещё не существует.
// Это родительская таблица для document_descriptions и extracted_fields —
// обе ссылаются на неё через внешний ключ document_id, поэтому данный
// метод должен вызываться раньше их createTable().
void DocumentDescriptionRepository::createDocumentsTable() {
    pqxx::work txn(connection_);
    txn.exec(
        "CREATE TABLE IF NOT EXISTS documents ("
        "id SERIAL PRIMARY KEY,"
        "user_id INT NOT NULL,"
        "original_filename TEXT NOT NULL,"
        "file_path TEXT NOT NULL,"
        "file_type VARCHAR(20) NOT NULL,"
        "file_size BIGINT NOT NULL DEFAULT 0,"
        "created_at TIMESTAMP NOT NULL DEFAULT NOW()"
        ");"
    );
    txn.commit();
}

// Создаёт таблицу document_descriptions, если она ещё не существует.
// IF NOT EXISTS означает: "если такая таблица уже есть — ничего не делать,
// ошибки не будет".
void DocumentDescriptionRepository::createTable() {
    pqxx::work txn(connection_); // открываем транзакцию — своего рода "черновик" изменений
    txn.exec(
        "CREATE TABLE IF NOT EXISTS document_descriptions ("
        "id SERIAL PRIMARY KEY,"                                                 // номер записи, создаётся автоматически
        "document_id INT NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
        "original_document TEXT NOT NULL,"  // ссылка на документ; если документ удалят, описание удалится тоже
        "ai_description TEXT NOT NULL,"                                          // описание от ИИ
        "regex_description TEXT NOT NULL,"                                       // описание из регулярных выражений
        "created_at TIMESTAMP NOT NULL DEFAULT NOW()"                            // когда запись была создана
        ");"
    );
    txn.commit(); // подтверждаем изменения — только теперь они реально применяются в БД
}

// Добавляет новую запись в таблицу и возвращает id, который база данных
// присвоила этой записи.
int DocumentDescriptionRepository::create(int documentId, const std::string& originalDocument,
    const std::string& aiDescription, const std::string& regexDescription) {
    pqxx::work txn(connection_);

    // $1, $2, $3, $4 — это "заглушки" для значений. Они безопасно подставляются
    // библиотекой pqxx через pqxx::params, поэтому текст описания не может
    // случайно сломать SQL-запрос (даже если в нём есть кавычки или другие
    // спецсимволы).
    pqxx::result res = txn.exec(
        "INSERT INTO document_descriptions (document_id, original_document, ai_description, regex_description) "
        "VALUES ($1, $2, $3, $4) RETURNING id;",
        pqxx::params{ documentId, originalDocument, aiDescription, regexDescription }
    );
    txn.commit();
    return res[0][0].as<int>(); // достаём id только что созданной записи
}

// Вспомогательная функция, видимая только внутри этого файла (анонимное
// пространство имён). Превращает одну строку результата SQL-запроса
// в удобный объект DocumentDescription.
    // Тип pqxx::row_ref (а не pqxx::row) используется потому, что именно
    // его возвращает библиотека при обращении к строкам результата запроса
    // в используемой версии libpqxx (8.0 и новее).
namespace {
    template <typename T>
    DocumentDescription convertToDescription(const T& row) {
        DocumentDescription d;
        d.id = row["id"].template as<int>();
        d.documentId = row["document_id"].template as<int>();
        d.aiDescription = row["ai_description"].template as<std::string>();
        d.regexDescription = row["regex_description"].template as<std::string>();
        d.createdAt = row["created_at"].template as<std::string>();
        return d;
    }
}


// Ищет в базе данных описание конкретного документа.
// Если для этого документа ещё ничего не сохранено — возвращает std::nullopt.
std::optional<DocumentDescription> DocumentDescriptionRepository::getByDocumentId(int documentId) {
    pqxx::work txn(connection_);
    pqxx::result res = txn.exec(
        "SELECT * FROM document_descriptions WHERE document_id = $1;",
        pqxx::params{ documentId }
    );
    if (res.empty()) return std::nullopt; // ничего не найдено — сообщаем об этом явно

    // Вызываем правильное, новое имя функции без всяких префиксов
    return std::make_optional(convertToDescription(res[0]));
}

// Возвращает вообще все записи из таблицы document_descriptions,
// отсортированные по id.
std::vector<DocumentDescription> DocumentDescriptionRepository::getAll() {
    pqxx::work txn(connection_);
    pqxx::result res = txn.exec("SELECT * FROM document_descriptions ORDER BY id;");

    std::vector<DocumentDescription> descriptions;
    descriptions.reserve(res.size()); // заранее выделяем память под нужное число элементов
    for (const auto& row : res) {
        // И здесь тоже меняем на convertToDescription
        descriptions.push_back(convertToDescription(row));
    }
    return descriptions;
}

int DocumentDescriptionRepository::addEmptyDocument() {
    pqxx::work tx(connection_);

    // Шаг 1. Создаём "заготовку" документа в родительской таблице documents.
    // Именно на эту таблицу ссылается внешний ключ document_id в таблице
    // document_descriptions, поэтому запись здесь должна появиться первой.
    // Значения ниже — временные заглушки, так как на момент создания
    // пустого документа реальные данные файла (имя, путь, размер) ещё
    // неизвестны. При необходимости они обновляются позже, отдельным
    // методом обновления записи.
    pqxx::result docRes = tx.exec(
        "INSERT INTO documents (user_id, original_filename, file_path, file_type, file_size) "
        "VALUES ($1, $2, $3, $4, $5) RETURNING id;",
        pqxx::params{ 1, "unknown", "unknown", "jpg", 0 }
    );
    int documentId = docRes[0][0].as<int>();

    // Шаг 2. Создаём связанную "заготовку" описания в document_descriptions,
    // ссылающуюся на только что созданный документ через document_id.
    // Именно эту запись впоследствии найдёт repo.getByDocumentId(documentId).
    // На этом этапе реальный текст документа ещё не получен (OCR ещё не
    // выполнялся), поэтому для original_document также используется
    // временная заглушка — она будет заменена позже, при вызове create().
    tx.exec(
        "INSERT INTO document_descriptions (document_id, original_document, ai_description, regex_description) "
        "VALUES ($1, $2, $3, $4);",
        pqxx::params{ documentId, "Ожидает обработки...", "В процессе обработки...", "В процессе обработки..." }
    );

    tx.commit();

    // Возвращаем id документа (не id описания) — именно он используется
    // дальше в пайплайне (имя файла изображения, повторный поиск описания).
    return documentId;
}