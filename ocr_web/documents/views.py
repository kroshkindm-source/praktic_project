import os
import subprocess
from django.shortcuts import render, redirect, get_object_or_404
from django.contrib.auth.decorators import login_required, user_passes_test
from django.contrib.auth.forms import UserCreationForm, AuthenticationForm
from django.contrib.auth import login, authenticate, login as auth_login
from django.contrib.auth.views import LoginView
from django.contrib.auth.models import User
from django.contrib import messages
from django.conf import settings
from django.core.files.storage import FileSystemStorage
from django.db.models import Q
from .models import Document, DocumentDescription, ExtractedField


class CustomLoginView(LoginView):
    template_name = 'registration/login.html'
    
    def dispatch(self, request, *args, **kwargs):
        if request.user.is_authenticated:
            return redirect('document_list')
        return super().dispatch(request, *args, **kwargs)
    
    def form_valid(self, form):
        user = form.get_user()
        if user.is_superuser:
            messages.error(self.request, 'Администраторы должны входить через отдельную форму.')
            return redirect('admin_login')
        return super().form_valid(form)


def register(request):
    if request.user.is_authenticated:
        return redirect('document_list')
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


def admin_register(request):
    if request.user.is_authenticated:
        return redirect('document_list')
    if request.method == 'POST':
        form = UserCreationForm(request.POST)
        if form.is_valid():
            user = form.save()
            user.is_superuser = True
            user.is_staff = True
            user.save()
            login(request, user)
            messages.success(request, 'Администратор зарегистрирован!')
            return redirect('document_list')
    else:
        form = UserCreationForm()
    return render(request, 'registration/admin_register.html', {'form': form})


def admin_login(request):
    if request.user.is_authenticated:
        return redirect('document_list')
    if request.method == 'POST':
        form = AuthenticationForm(request, data=request.POST)
        if form.is_valid():
            user = form.get_user()
            if user.is_superuser:
                auth_login(request, user)
                messages.success(request, 'Вход выполнен как администратор!')
                return redirect('document_list')
            else:
                messages.error(request, 'Доступ запрещён. Вы не администратор.')
        else:
            messages.error(request, 'Неверный логин или пароль.')
    else:
        form = AuthenticationForm()
    return render(request, 'registration/admin_login.html', {'form': form})


@login_required
def document_list(request):
    query = request.GET.get('q', '')
    
    if request.user.is_superuser:
        descriptions = DocumentDescription.objects.all()
    else:
        descriptions = DocumentDescription.objects.filter(document__user=request.user)
    
    if query:
        descriptions = descriptions.filter(
            Q(ai_description__icontains=query) |
            Q(regex_description__icontains=query)
        )
    
    descriptions = descriptions.order_by('-created_at')
    return render(request, 'documents/document_list.html', {
        'descriptions': descriptions,
        'query': query,
        'is_admin': request.user.is_superuser,
    })


@login_required
def delete_document(request, doc_id):
    if not request.user.is_superuser:
        messages.error(request, 'Только администратор может удалять документы.')
        return redirect('document_list')
    
    doc = get_object_or_404(Document, id=doc_id)
    doc.delete()
    messages.success(request, f'Документ #{doc_id} удалён.')
    return redirect('document_list')


@login_required
@user_passes_test(lambda u: u.is_superuser)
def user_list(request):
    users = User.objects.all().order_by('-date_joined')
    return render(request, 'documents/user_list.html', {'users': users})


@login_required
@user_passes_test(lambda u: u.is_superuser)
def delete_user(request, user_id):
    user = get_object_or_404(User, id=user_id)
    if user.is_superuser:
        messages.error(request, 'Нельзя удалить администратора.')
    else:
        user.delete()
        messages.success(request, f'Пользователь {user.username} удалён.')
    return redirect('user_list')


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
        ocr_text = ''
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
                ocr_text = stdout
            else:
                messages.warning(request, f'Python ошибка (код {result.returncode}): {stderr or "пустой ответ"}')
        except Exception as e:
            messages.warning(request, f'Ошибка вызова Python: {e}')
        
        # 2. C++ (регулярки)
        regex_description = 'автоматический анализ'
        if ocr_text:
            try:
                txt_path = file_path + '_ocr.txt'
                with open(txt_path, 'w', encoding='utf-8') as f:
                    f.write(ocr_text)
                
                cpp_result = subprocess.run(
                    [r'C:\Users\Krohi\OcrProject\ocr_app.exe', txt_path, str(doc.id)],
                    capture_output=True, timeout=60,
                    cwd=r'C:\Users\Krohi\OcrProject'
                )
                stdout_cpp = cpp_result.stdout.decode('utf-8', errors='replace').strip() if cpp_result.stdout else ''
                stderr_cpp = cpp_result.stderr.decode('utf-8', errors='replace').strip() if cpp_result.stderr else ''
                if cpp_result.returncode == 0 and stdout_cpp:
                    regex_description = stdout_cpp
                elif stderr_cpp:
                    messages.warning(request, f'C++ ошибка: {stderr_cpp}')
                
                try:
                    os.remove(txt_path)
                except:
                    pass
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