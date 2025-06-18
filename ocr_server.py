import asyncio
import websockets
import threading
from flask import Flask, request, jsonify
import easyocr
import os
import re
from datetime import datetime
import logging

# Configurar logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

app = Flask(__name__)

# Inicializar EasyOCR (español e inglés)
reader = easyocr.Reader(['es'])

# Lista para mantener conexiones WebSocket
websocket_connections = set()


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))     # Obtener el directorio donde está el script (carpeta actual)
PHOTO_NAME = "photo.jpg"  # Nombre fijo para la imagen
OCR_TEXT_NAME = "ocr_result.txt"  # Nombre fijo para el resultado

def format_text_with_hash(text):
    """
    Añade # antes de números o secuencias numéricas en el texto, evitando doble ##
    Ejemplos corregidos:
    - 'hola123' -> 'hola#123'
    - 'hola 123' -> 'hola #123'
    - 'precio123.45' -> 'precio#123.45'
    - 'mundo##1234' -> 'mundo#1234'  # Corrige doble #
    """
    # Primero normalizamos posibles dobles # existentes
    text = re.sub(r'##+', '#', text)
    
    # Patrón mejorado que maneja:
    # 1. Números pegados a palabras (palabra123)
    # 2. Números con decimales (123.45)
    # 3. Evita añadir # si ya existe uno
    pattern = r'(?<!#)(\b\w*?)(\d+(?:\.\d+)?)'
    
    def replacer(match):
        prefix = match.group(1)
        number = match.group(2)
        
        # Si el prefijo termina en #, no añadir otro
        if prefix.endswith('#'):
            return f"{prefix}{number}"
        # Si hay texto antes del número
        elif prefix:
            return f"{prefix}#{number}"
        # Si es solo el número
        else:
            return f"#{number}"
    
    # Manejar números pegados a palabras (sin espacio)
    text = re.sub(pattern, replacer, text)
    
    # Manejar números con espacio previo (solo si no hay # ya)
    text = re.sub(r'(?<!#)(\w)\s+(\d+(?:\.\d+)?)', r'\1 #\2', text)
    
    return text


@app.route('/upload', methods=['POST'])
def upload_image():
    try:
        # Verificar si se recibió un archivo (ESP32-CAM puede usar multipart/form-data)
        if 'file' in request.files:
            file = request.files['file']
            if file.filename == '':
                return jsonify({"error": "No se selecciono archivo"}), 400
            
            image_data = file.read()
        else:
            # Si no viene como multipart, tomar los datos directamente
            image_data = request.get_data()
            if not image_data:
                return jsonify({"error": "No se recibio imagen"}), 400

        # Verificar que los datos son una imagen JPEG válida
        if not image_data.startswith(b'\xff\xd8'):
            logger.error("Datos recibidos no son una imagen JPEG valida")
            return jsonify({"error": "Formato de imagen no valido, se esperaba JPEG"}), 400

        # Guardar imagen (sobreescribiendo)
        filepath = os.path.join(SCRIPT_DIR, PHOTO_NAME)
        with open(filepath, 'wb') as f:
            f.write(image_data)
        
        logger.info(f"Imagen JPEG recibida y guardada: {filepath}")
        
        threading.Thread(target=process_ocr, args=(filepath,)).start()
        return jsonify({"message": "Imagen recibida y procesandose"}), 200
        
    except Exception as e:
        logger.error(f"Error procesando imagen: {str(e)}", exc_info=True)
        return jsonify({"error": str(e)}), 500

def process_ocr(image_path):
    try:
        logger.info(f"Iniciando OCR para: {image_path}")
        
        if not os.path.exists(image_path):
            error_msg = f"El archivo no existe: {image_path}"
            logger.error(error_msg)
            asyncio.run(send_text_to_esp32(error_msg))
            return

        with open(image_path, 'rb') as f:
            image_bytes = f.read()
        
        import numpy as np
        from PIL import Image, ImageFilter
        import io
        
        try:
            # Abrir imagen y convertir a array numpy
            img = Image.open(io.BytesIO(image_bytes))
            
            # Preprocesamiento mejorado de la imagen
            img = img.convert('L')  # Convertir a escala de grises
            img = img.filter(ImageFilter.SHARPEN)  # Mejorar nitidez
            
            # Redimensionar (si es necesario) usando el nuevo método LANCZOS
            if img.size[0] > 1000 or img.size[1] > 1000:
                new_size = (img.size[0]//2, img.size[1]//2)
                img = img.resize(new_size, Image.Resampling.LANCZOS)  # Reemplazo de ANTIALIAS
            
            img_array = np.array(img)
            
        except Exception as e:
            error_msg = f"Error al procesar la imagen: {str(e)}"
            logger.error(error_msg, exc_info=True)
            asyncio.run(send_text_to_esp32(error_msg))
            return

        # Procesar OCR con parámetros optimizados
        print("\n[=== PROCESANDO IMAGEN... ===]")
        results = reader.readtext(
            img_array,
            decoder='beamsearch',  # Alternativa: 'greedy'
            batch_size=1,
            workers=0,
            detail=1
        )
        
        # Filtrar y formatear resultados
        valid_texts = [
            f"{format_text_with_hash(text.strip())}"
            for (bbox, text, confidence) in results 
            if confidence > 0.4
        ]
        full_text = " ".join(valid_texts) if valid_texts else "Error no se detecto texto con suficiente confianza"
        
        print("\n[=== RESULTADO OCR ===]")
        print("=" * 50)
        print(full_text)
        print("=" * 50)
        
        asyncio.run(send_text_to_esp32(full_text))
        
        # Guardar resultado
        text_filename = os.path.join(SCRIPT_DIR, OCR_TEXT_NAME)
        with open(text_filename, 'w', encoding='utf-8') as f:
            f.write(full_text)
            
    except Exception as e:
        error_msg = f"Error fatal en OCR: {str(e)}"
        logger.error(error_msg, exc_info=True)
        print(f"\n[=== ERROR CRITICO ===]\n{error_msg}")
        asyncio.run(send_text_to_esp32(error_msg))


async def send_text_to_esp32(text):
    if websocket_connections:
        logger.info(f"Enviando texto a {len(websocket_connections)} conexiones WebSocket")
        # Enviar a todas las conexiones activas
        disconnected = set()
        for websocket in websocket_connections.copy():
            try:
                await websocket.send(text)
            except websockets.exceptions.ConnectionClosed:
                disconnected.add(websocket)
        
        # Remover conexiones cerradas
        websocket_connections.difference_update(disconnected)
    else:
        logger.warning("No hay conexiones WebSocket activas")

async def websocket_handler(websocket, path):
    logger.info(f"Nueva conexión WebSocket desde: {websocket.remote_address}")
    websocket_connections.add(websocket)
    try:
        await websocket.wait_closed()
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        websocket_connections.discard(websocket)
        logger.info(f"Conexión WebSocket cerrada: {websocket.remote_address}")

def run_flask():
    app.run(host='0.0.0.0', port=8080, debug=False)


def run_websocket():
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    
    # Cambia el puerto a 8081
    start_server = websockets.serve(websocket_handler, "0.0.0.0", 8081)
    
    try:
        print(f"Servidor WebSocket iniciado en ws://0.0.0.0:8081")
        loop.run_until_complete(start_server)
        loop.run_forever()
    except KeyboardInterrupt:
        print("\nWebSocket server stopped")
    finally:
        loop.close()

if __name__ == '__main__':
    print("=== SERVIDOR ESP32-CAM OCR ===")
    print("Iniciando servidor...")
    print(f"- Directorio de imagenes: {SCRIPT_DIR}")
    print(f"- Imagenes guardadas como: {PHOTO_NAME}")
    print(f"- Resultados OCR guardados como: {OCR_TEXT_NAME}")
    print(f"- Puerto HTTP (Flask): 8080")
    print(f"- Puerto WebSocket: 8081")  # Añade esta línea
    print("- Idiomas OCR: ES, EN")
    print("===============================")
    
    flask_thread = threading.Thread(target=run_flask)
    websocket_thread = threading.Thread(target=run_websocket)
    
    flask_thread.daemon = True
    websocket_thread.daemon = True
    
    flask_thread.start()
    websocket_thread.start()
    
    try:
        while True:
            threading.Event().wait(1)
    except KeyboardInterrupt:
        print("\nCerrando servidor...")
        exit(0)