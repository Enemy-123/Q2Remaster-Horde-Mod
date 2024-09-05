import os
import chardet

def convert_to_utf8(file_path):
    # Detectar la codificación original del archivo
    with open(file_path, 'rb') as file:
        raw_data = file.read()
        encoding = chardet.detect(raw_data)['encoding']
    
    if encoding != 'utf-8':
        # Leer el archivo con la codificación detectada
        with open(file_path, 'r', encoding=encoding) as file:
            content = file.read()
        
        # Escribir el contenido de nuevo en el archivo en UTF-8
        with open(file_path, 'w', encoding='utf-8') as file:
            file.write(content)
        print(f"Convertido a UTF-8: {file_path}")
    else:
        print(f"Ya está en UTF-8: {file_path}")

def convert_directory_to_utf8(directory):
    for root, _, files in os.walk(directory):
        for file_name in files:
            if file_name.endswith(('.cpp', '.h')):  # Puedes agregar más extensiones si es necesario
                file_path = os.path.join(root, file_name)
                convert_to_utf8(file_path)

# Ruta al directorio de tu proyecto
project_directory = r'C:\Users\pipev\source\repos\Q2Remaster-Horde-Mod'

convert_directory_to_utf8(project_directory)
