import os
import subprocess
from django.shortcuts import render, redirect, get_object_or_404
from django.contrib.auth.decorators import login_required
from django.contrib.auth.forms import UserCreationForm
from django.contrib.auth import login
from django.contrib import messages
from django.conf import settings
from django.core.files.storage import FileSystemStorage
from .models import Document, DocumentDescription, ExtractedField


def register(request):
    if request.method == 'POST':
        form = UserCreationForm(request.POST)
        if form.is_valid():
            user = form.save()
            login(request, user)
            messages.success(request, 'Регистрация успешна!')
            return redirect('document_list')
    else:
        form = UserCreationForm()
    return render(request, 'registration/register.html', {'form': form})


@login_required
def document_list(request):
    if request.user.is_superuser:
        descriptions = DocumentDescription.objects.all().order_by('-created_at')
    else:
        descriptions = DocumentDescription.objects.filter(
            document__user=request.user
        ).order_by('-created_at')
    return render(request, 'documents/document_list.html', {'descriptions': descriptions})


@login_required
def upload_document(request):
    if request.method == 'POST' and request.FILES.get('document'):
        uploaded_file = request.FILES['document']
        
        fs = FileSystemStorage(location=settings.MEDIA_ROOT)
        filename = fs.save(uploaded_file.name, uploaded_file)
        file_path = os.path.join(settings.MEDIA_ROOT, filename)
        
        doc = Document.objects.create(
            user=request.user,
            original_filename=uploaded_file.name,
            file_path=file_path,
            file_type=os.path.splitext(uploaded_file.name)[1].replace('.', ''),
            file_size=uploaded_file.size,
        )
        
        # 1. Python (OCR + ИИ)
        ai_description = 'Не удалось получить ответ ИИ'
        try:
            result = subprocess.run(
                [r'C:\Users\Krohi\OcrProject\.venv\Scripts\python.exe', 'test.py', file_path],
                capture_output=True, timeout=120,
                cwd=r'C:\Users\Krohi\OcrProject'
            )
            stdout = result.stdout.decode('utf-8', errors='replace').strip() if result.stdout else ''
            stderr = result.stderr.decode('utf-8', errors='replace').strip() if result.stderr else ''
            if result.returncode == 0 and stdout:
                ai_description = stdout
            else:
                messages.warning(request, f'Python ошибка (код {result.returncode}): {stderr or "пустой ответ"}')
        except Exception as e:
            messages.warning(request, f'Ошибка вызова Python: {e}')
        
        # 2. C++ (регулярки)
        regex_description = 'автоматический анализ'
        try:
            cpp_result = subprocess.run(
                [r'C:\Users\Krohi\OcrProject\ocr_app.exe', file_path, str(doc.id)],
                capture_output=True, timeout=60,
                cwd=r'C:\Users\Krohi\OcrProject'
            )
            stdout = cpp_result.stdout.decode('utf-8', errors='replace').strip() if cpp_result.stdout else ''
            stderr = cpp_result.stderr.decode('utf-8', errors='replace').strip() if cpp_result.stderr else ''
            if cpp_result.returncode == 0 and stdout:
                regex_description = stdout
            elif stderr:
                messages.warning(request, f'C++ ошибка: {stderr}')
        except Exception as e:
            messages.warning(request, f'Ошибка вызова C++: {e}')
        
        DocumentDescription.objects.create(
            document=doc,
            original_document='',
            ai_description=ai_description,
            regex_description=regex_description,
        )
        
        messages.success(request, 'Документ успешно обработан!')
        return redirect('document_detail', doc_id=doc.id)
        
    return render(request, 'documents/upload.html')


@login_required
def document_detail(request, doc_id):
    desc = get_object_or_404(DocumentDescription, document_id=doc_id)
    fields = ExtractedField.objects.filter(document_id=doc_id)
    return render(request, 'documents/document_detail.html', {
        'desc': desc,
        'fields': fields,
    })