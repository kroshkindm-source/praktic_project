#pragma once
#include <string>
#include <vector>
#include <optional>
#include <pqxx/pqxx>

// Это описание ОДНОЙ записи о документе
struct DocumentDescription {
    int id = 0;                        // номер записи в базе данных (создаётся автоматически)
    int documentId = 0;                // к какому документу относится это описание
    std::string aiDescription;         // описание, которое сгенерировал ИИ (например, языковая модель)
    std::string regexDescription;      // описание, собранное из значений, найденных регулярными выражениями
    std::string createdAt;             // дата и время создания записи
};

// Этот класс отвечает за работу с таблицей document_descriptions:
// создание таблицы, добавление новой записи, чтение и удаление записей.

class DocumentDescriptionRepository {
public:
    // При создании объекта нужно передать строку подключения к БД
    explicit DocumentDescriptionRepository(const std::string& connectionString);

    // Создаёт таблицу documents в базе данных
    void createDocumentsTable();

    // Создаёт таблицу document_descriptions 
    void createTable();

    // Регистрирует новый документ в таблице documents 
    int addEmptyDocument();

    // Добавляет новую запись
    int create(int documentId, const std::string& originalDocument,
        const std::string& aiDescription, const std::string& regexDescription);

    // Возвращает описание конкретного документа
    std::optional<DocumentDescription> getByDocumentId(int documentId);

    void update(int documentId, const std::string& originalDocument, 
                                          const std::string& aiDescription, const std::string& regexDescription) {
    pqxx::work txn(connection_);
    
    txn.exec(
        "UPDATE document_descriptions SET original_document = $2, ai_description = $3, regex_description = $4 "
        "WHERE document_id = $1;",
        pqxx::params{ documentId, originalDocument, aiDescription, regexDescription }
    );
    
        txn.commit();
    }
    // Возвращает все записи из таблицы document_descriptions.
    std::vector<DocumentDescription> getAll();

    // Удаляет одну запись по её id.
    bool remove(int id);

    void addPath(int ID, const std::string& newPath) {
        pqxx::work tx(connection_);
        tx.exec("UPDATE documents SET file_path =" + tx.quote(newPath) + " WHERE id = " + tx.quote(ID));
        tx.commit();
    }

private:
    pqxx::connection connection_; // соединение с базой данных, открыто на весь срок жизни объекта
};