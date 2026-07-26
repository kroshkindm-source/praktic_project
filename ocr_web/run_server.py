import os
import sys

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'ocr_web.settings')

from django.core.wsgi import get_wsgi_application
application = get_wsgi_application()

if __name__ == '__main__':
    from django.core.management import execute_from_command_line
    execute_from_command_line(sys.argv)