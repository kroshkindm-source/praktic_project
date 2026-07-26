import os
import sys
import waitress
from run_server import application

if __name__ == '__main__':
    os.chdir(r'C:\Users\Krohi\OcrProject\ocr_web')
    waitress.serve(application, host='127.0.0.1', port=8000)