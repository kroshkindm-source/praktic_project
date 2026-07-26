import os
import sys
import io
import requests
from dotenv import load_dotenv

# НЕ переопределяем sys.stdout — subprocess не сможет читать
# Вместо этого просто задаём кодировку для консоли
if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')
    sys.stderr.reconfigure(encoding='utf-8')

def process_text_with_ai(file_path):
    file_path = file_path.strip('"').strip("'")
    
    # Загружаем ключ
    script_dir = os.path.dirname(os.path.abspath(__file__))
    load_dotenv(dotenv_path=os.path.join(script_dir, '.env'))
    api_key = os.getenv("YANDEX_API_KEY") or os.getenv("YANDEX_IAM_TOKEN")
    
    if not api_key:
        print("Ошибка: Ключ YandexGPT не найден в .env!", file=sys.stderr)
        sys.exit(1)
    
    # Читаем файл
    try:
        if not os.path.exists(file_path):
            print(f"Ошибка: Файл не найден: {file_path}", file=sys.stderr)
            sys.exit(1)
        
        # Пробуем читать как текст
        with open(file_path, 'r', encoding='utf-8') as f:
            ocr_text = f.read()
    except UnicodeDecodeError:
        # Если не текст — OCR через pytesseract
        try:
            import pytesseract
            from PIL import Image
            pytesseract.pytesseract.tesseract_cmd = r'C:\Program Files\Tesseract-OCR\tesseract.exe'
            img = Image.open(file_path)
            ocr_text = pytesseract.image_to_string(img, lang='rus+eng')
        except Exception as e:
            print(f"Ошибка OCR: {e}", file=sys.stderr)
            sys.exit(1)
    
    if not ocr_text.strip():
        print("Ошибка: Текст не распознан.", file=sys.stderr)
        sys.exit(1)
    
    # Запрос к YandexGPT
    prompt = (
        "Ты — ИИ-аналитик. Извлеки данные из текста в формате 'ключ: значение'.\n"
        "Правила: только формат 'ключ: значение', без вводных слов и пояснений.\n"
        "Извлекай только самую ключевую информацию по типу дат, имён, адресов, сумм\n"
        f"Текст:\n{ocr_text}"
    )
    
    if api_key.startswith("t1."):
        headers = {"Authorization": f"Bearer {api_key}"}
    else:
        headers = {"Authorization": f"Api-Key {api_key}"}
    
    payload = {
        "modelUri": "gpt://b1gm1ufqbq1d5t5q1mnk/yandexgpt/latest",
        "completionOptions": {"temperature": 0.3, "maxTokens": 2000},
        "messages": [{"role": "user", "text": prompt}]
    }
    
    try:
        response = requests.post(
            "https://llm.api.cloud.yandex.net/foundationModels/v1/completion",
            headers=headers,
            json=payload,
            timeout=60
        )
        response.raise_for_status()
        result = response.json()
        
        answer = result.get("result", {}).get("alternatives", [{}])[0].get("message", {}).get("text", "")
        if answer:
            print(answer)
        else:
            print("Ошибка: пустой ответ от YandexGPT", file=sys.stderr)
            sys.exit(1)
            
    except requests.exceptions.RequestException as e:
        print(f"Ошибка API YandexGPT: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    if len(sys.argv) > 1:
        process_text_with_ai(sys.argv[1])
    else:
        print("Ошибка: не указан путь к файлу.", file=sys.stderr)