from django.db import models

# Create your models here.
from django.db import models

class Document(models.Model):
    user = models.ForeignKey('auth.User', on_delete=models.CASCADE)
    original_filename = models.TextField()
    file_path = models.TextField()
    file_type = models.CharField(max_length=20)
    file_size = models.BigIntegerField(default=0)
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        db_table = 'documents'  # твоя существующая таблица
        managed = False         # не трогать структуру таблицы


class DocumentDescription(models.Model):
    document = models.ForeignKey(Document, on_delete=models.CASCADE, db_column='document_id')
    original_document = models.TextField()
    ai_description = models.TextField()
    regex_description = models.TextField()
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        db_table = 'document_descriptions'
        managed = False


class ExtractedField(models.Model):
    document = models.ForeignKey(Document, on_delete=models.CASCADE, db_column='document_id')
    field_name = models.CharField(max_length=50)
    field_value = models.TextField()
    confidence = models.DecimalField(max_digits=5, decimal_places=2, default=0)
    extracted_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        db_table = 'extracted_fields'
        managed = False