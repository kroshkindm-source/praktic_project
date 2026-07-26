#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include "FieldExtractionService.h"

using namespace std;

string readTextFile(const string& path) {
    ifstream file(path, ios::binary);
    if (!file.is_open()) {
        cerr << "Cannot open file: " << path << endl;
        exit(1);
    }
    stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: ocr_app.exe <txt_file_path> <document_id>" << endl;
        return 1;
    }

    string filePath = argv[1];
    int docId = stoi(argv[2]);

    try {
        FieldExtractionService service("host=127.0.0.1 dbname=postgres user=postgres");
        
        string ocrText = readTextFile(filePath);
        string result = service.extractAndSave(docId, ocrText);
        
        cout << result << endl;
        
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}