import os
import requests
import json
import sys
import io
from dotenv import load_dotenv
# Обеспечиваем корректную работу с кодировкой UTF-8 для любого вывода
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

def process_text_with_ai(file_path):
    # 1. Загрузка ключа
    script_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(script_dir)

    # Теперь загружаем .env именно из этой папки
    load_dotenv(dotenv_path=os.path.join(script_dir, '.env'))

    api = os.getenv("GCP_API_KEY")

    # Давай добавим диагностику, чтобы увидеть, что происходит
    if not api:
        print(f"DEBUG: Путь к скрипту: {script_dir}", file=sys.stderr)
        print(f"DEBUG: Файл .env найден: {os.path.exists('.env')}", file=sys.stderr)
        print("Ошибка: Переменная GCP_API_KEY не найдена!", file=sys.stderr)
        sys.exit(1)

    with open(file_path, "r", encoding="utf-8") as f:
        ocr_text = f.read()
    
    # 3. Формирование URL и Заголовков
    # Для API Google обязательно нужен заголовок Content-Type: application/json
    url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key={api}"
    headers = {"Content-Type": "application/json"}
    
    prompt = (
        "Ты — продвинутый ИИ-аналитик. Твоя задача — извлечь из текста всю ключевую информацию и представить её СТРОГО в виде плоского списка пар 'ключ: значение'.\n"
        "ПРАВИЛА ОТВЕТА:\n"
        "1. Пиши ТОЛЬКО в формате: `название_поля: значение`.\n"
        "2. Никаких вводных слов, пояснений, здорований и заключений. Только сухие данные.\n"
        "3. Используй понятные и логичные ключи (например: date, type, ФИО, сумма, организация, номер_документа и т.д.).\n"
        "4. Исправляй очевидные опечатки распознавания OCR при переносе данных.\n"
        "5. Выделяй только самые важные фрагменты, только ключевые моменты по типу даты договора, имён, типа договора.\n\n"
        f"Вот текст для анализа:\n{ocr_text}"
    )
    
    payload = {
        "contents": [{"parts": [{"text": prompt}]}],
        "safetySettings": [
            {"category": "HARM_CATEGORY_HARASSMENT", "threshold": "BLOCK_NONE"},
            {"category": "HARM_CATEGORY_HATE_SPEECH", "threshold": "BLOCK_NONE"},
            {"category": "HARM_CATEGORY_SEXUALLY_EXPLICIT", "threshold": "BLOCK_NONE"},
            {"category": "HARM_CATEGORY_DANGEROUS_CONTENT", "threshold": "BLOCK_NONE"},
            {"category": "HARM_CATEGORY_CIVIC_INTEGRITY", "threshold": "BLOCK_NONE"}
        ]
    }
    
    # 4. Выполнение запроса
    try:
        response = requests.post(url, headers=headers, json=payload)
        response.raise_for_status() # Вызовет исключение, если статус не 200
        result = response.json()
        
        # Безопасный парсинг ответа
        text_content = result.get('candidates', [{}])[0].get('content', {}).get('parts', [{}])[0].get('text')
        
        if text_content:
            print(text_content) # Выводим только текст для C++
        else:
            print("Ошибка: ИИ вернул пустой текст", file=sys.stderr)
            
    except Exception as e:
        print(f"Ошибка при обращении к ИИ: {e}", file=sys.stderr)

if __name__ == "__main__":
    # Проверяем, передан ли аргумент из C++ (sys.argv[1])
    if len(sys.argv) > 1:
        file_to_process = sys.argv[1]
        process_text_with_ai(file_to_process)
    else:
        # Если скрипт запустили вручную без аргументов
        print("Ошибка: не указан путь к файлу. Используйте: python script.py <имя_файла>")