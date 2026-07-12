#pragma once
#include <string>
#include <vector>
#include <optional>
#include <pqxx/pqxx>

// Это описание ОДНОЙ записи о документе: как этот документ описал ИИ,
// и как его же описали данные, извлечённые регулярными выражениями.
// Каждая такая запись хранится в базе данных как отдельная строка
// таблицы document_descriptions.
struct DocumentDescription {
    int id = 0;                        // номер записи в базе данных (создаётся автоматически)
    int documentId = 0;                // к какому документу относится это описание
    std::string aiDescription;         // описание, которое сгенерировал ИИ (например, языковая модель)
    std::string regexDescription;      // описание, собранное из значений, найденных регулярными выражениями
    std::string createdAt;             // дата и время создания записи
};

// Этот класс отвечает за работу с таблицей document_descriptions:
// создание таблицы, добавление новой записи, чтение и удаление записей.
//
// Соединение с базой данных хранится прямо внутри класса (поле connection_)
// и открывается один раз — при создании объекта. Так не приходится
// каждый раз заново подключаться к базе при каждом вызове метода.
class DocumentDescriptionRepository {
public:
    // При создании объекта нужно передать строку подключения к БД, например:
    // "dbname=ocr_db user=postgres password=postgres host=localhost port=5432"
    explicit DocumentDescriptionRepository(const std::string& connectionString);

    // Создаёт таблицу document_descriptions в базе данных, если её ещё нет.
    // Вызывать один раз при первом запуске программы.
    void createTable();

    // Регистрирует новый документ в таблице documents ещё ДО того, как он
    // прошёл OCR-обработку, и возвращает id, который база данных присвоила
    // этой записи. Этот id дальше используется, чтобы найти файл документа
    // на диске (см. ocrLogic) и чтобы привязать к документу его будущее
    // описание (см. create()). Реальные данные о файле (владелец, имя,
    // путь, тип, размер) на этом шаге ещё не известны, поэтому записываются
    // как временная заглушка — их можно будет обновить позже, когда файл
    // будет фактически сохранён и обработан.
    int addEmptyDocument();

    // Добавляет новую запись: описание документа от ИИ и описание,
    // собранное из регулярных выражений. Возвращает id новой записи.
    int create(int documentId, const std::string& aiDescription, const std::string& regexDescription);

    // Возвращает описание конкретного документа, если оно есть в базе.
    // Если для документа ещё ничего не сохранено — возвращает "пусто" (std::nullopt).
    std::optional<DocumentDescription> getByDocumentId(int documentId);

    // Возвращает все записи из таблицы document_descriptions.
    std::vector<DocumentDescription> getAll();

    // Удаляет одну запись по её id.
    // Возвращает true, если запись действительно была удалена.
    bool remove(int id);

    void addPath(int ID, const std::string& newPath){
        pqxx::work tx(connection_);
        tx.exec("UPDATE documents SET file_path =" +tx.quote(newPath)+" WHERE id = " + tx.quote(ID));
        tx.commit();
    }

private:
    pqxx::connection connection_; // соединение с базой данных, открыто на весь срок жизни объекта
};
