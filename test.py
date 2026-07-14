import os
import sys
import io
import time
import pytesseract
from PIL import Image
from dotenv import load_dotenv
from google import genai
from google.genai.errors import APIError

# Настройка кодировки для C++
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', write_through=True)
sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', write_through=True)

# Укажите путь к Tesseract
pytesseract.pytesseract.tesseract_cmd = r'C:\Program Files\Tesseract-OCR\tesseract.exe'

def process_text_with_ai(file_path):
    # Очистка пути
    file_path = file_path.strip('"').strip("'")
    
    # 1. Загрузка ключа
    script_dir = os.path.dirname(os.path.abspath(__file__))
    load_dotenv(dotenv_path=os.path.join(script_dir, '.env'))
    api_key = os.getenv("GCP_API_KEY")

    if not api_key:
        print("Ошибка: Переменная GCP_API_KEY не найдена в .env!", file=sys.stderr)
        sys.exit(1)

    # 2. Распознавание текста (OCR)
    try:
        if not os.path.exists(file_path):
            print(f"Ошибка: Файл не найден: {file_path}", file=sys.stderr)
            sys.exit(1)

        img = Image.open(file_path)
        ocr_text = pytesseract.image_to_string(img, lang='rus+eng')
        
        if not ocr_text.strip():
            print("Ошибка: Текст на изображении не распознан.", file=sys.stderr)
            sys.exit(1)
            
    except Exception as e:
        print(f"Ошибка при работе с Tesseract: {e}", file=sys.stderr)
        sys.exit(1)

    # 3. Запрос к Gemini с механизмом повторных попыток
    client = genai.Client(api_key=api_key)
    prompt = (
        "Ты — ИИ-аналитик. Извлеки данные из текста в формате 'ключ: значение'.\n"
        "Правила: только формат 'ключ: значение', без вводных слов и пояснений.\n"
        f"Текст:\n{ocr_text}"
    )

    max_retries = 3
    for attempt in range(max_retries):
        try:
            response = client.models.generate_content(
                model='gemini-3.1-flash-lite',
                contents=prompt,
            )
            print(response.text)
            return # Успех, выходим
            
        except APIError as e:
            # Если ошибка 503, ждем и пробуем снова
            if e.code == 503 and attempt < max_retries - 1:
                time.sleep(2) # Пауза перед повтором
                continue
            else:
                print(f"Ошибка API при обращении к ИИ: {e}", file=sys.stderr)
                sys.exit(1)
        except Exception as e:
            print(f"Критическая ошибка: {e}", file=sys.stderr)
            sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) > 1:
        process_text_with_ai(sys.argv[1])
    else:
        print("Ошибка: не указан путь к файлу.", file=sys.stderr)