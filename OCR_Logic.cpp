#include "OCR_Logic.h"
#include "DocumentDescription.h"
#include <stdexcept>

using namespace std;

string ocrLogic(int docId, const string& imagePath, DocumentDescriptionRepository& repo) {
    auto docOpt = repo.getByDocumentId(docId);
    if (!docOpt.has_value()) {
        throw runtime_error("Document with ID: " + to_string(docId) + " not found in database");
    }

    string idStr = to_string(docId);
    string outputName = "doc_" + idStr;

    string command = "C:\\PROGRA~1\\Tesseract-OCR\\tesseract.exe \"" + imagePath + "\" \"" + outputName + "\" --tessdata-dir \"C:\\Program Files\\Tesseract-OCR\\tessdata\" -l rus+eng";

    int exitcode = system(command.c_str());
    if (exitcode != 0) {
        throw runtime_error("Tesseract error for ID: " + idStr);
    }

    return outputName + ".txt";
}