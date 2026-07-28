#!/usr/bin/env python3
import logging
import os
import psutil
import subprocess
import json
import datetime
from datetime import datetime
from pathlib import Path
from dotenv import load_dotenv
from telegram import Update, BotCommand
from telegram.ext import Application, CommandHandler, MessageHandler, filters, ContextTypes
from influxdb_client import InfluxDBClient
from influxdb_client.client.query_api import QueryApi

# Carga las variables de entorno desde un archivo .env local (no se sube al repo)
load_dotenv()

# Configuración de logging
logging.basicConfig(
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s', 
    level=logging.INFO,
    handlers=[
        logging.FileHandler('/var/log/monitoring_bot.log'),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger(__name__)

# Configuración
# Todas las credenciales se leen de variables de entorno (.env local).
# Ver .env.example para la plantilla de configuración.
CONFIG = {
    'bot_token': os.getenv('TELEGRAM_BOT_TOKEN'),
    'canal_id': int(os.getenv('TELEGRAM_CANAL_ID', '0')),
    'contact': {
        'name': 'Tito Xavier Zhanay Avila', 
        'email': os.getenv('CONTACT_EMAIL', 'tu-correo@ejemplo.com')
    },
    'thresholds': {
        'cpu_warning': 80,
        'memory_warning': 85,
        'disk_warning': 90,
        'temp_warning': 65,
        'temp_critical': 75
    },
    'services': ['grafana-server', 'influxdb', 'ssh', 'NetworkManager'],
    'influxdb': {
        'url': os.getenv('INFLUXDB_URL', 'http://localhost:8086'),
        'token': os.getenv('INFLUXDB_TOKEN'),
        'org': os.getenv('INFLUXDB_ORG', 'global'),
        'bucket': os.getenv('INFLUXDB_BUCKET', 'ESP32')
    }
}

# Conexión a InfluxDB
try:
    influx_client = InfluxDBClient(
        url=CONFIG['influxdb']['url'], 
        token=CONFIG['influxdb']['token'], 
        org=CONFIG['influxdb']['org']
    )
    query_api = influx_client.query_api()
    logger.info("Conexión a InfluxDB establecida")
except Exception as e:
    logger.error(f"Error conectando a InfluxDB: {e}")
    influx_client = None
    query_api = None

class SystemService:
    """Servicio de información del sistema"""
    
    @staticmethod
    def get_metrics():
        """Obtener métricas del sistema"""
        try:
            cpu_percent = psutil.cpu_percent(interval=1)
            memory = psutil.virtual_memory()
            disk = psutil.disk_usage('/')
            
            # Temperatura
            temp_celsius = 0
            try:
                with open('/sys/class/thermal/thermal_zone0/temp', 'r') as f:
                    temp_celsius = int(f.read().strip()) / 1000
            except (FileNotFoundError, ValueError):
                pass
            
            # Uptime
            with open('/proc/uptime', 'r') as f:
                uptime_seconds = float(f.readline().split()[0])
            
            days = int(uptime_seconds // 86400)
            hours = int((uptime_seconds % 86400) // 3600)
            minutes = int((uptime_seconds % 3600) // 60)
            
            uptime_parts = []
            if days > 0: uptime_parts.append(f"{days}d")
            if hours > 0: uptime_parts.append(f"{hours}h")
            if minutes > 0 or not uptime_parts: uptime_parts.append(f"{minutes}m")
            
            return {
                'cpu_percent': cpu_percent,
                'memory_percent': memory.percent,
                'memory_total_gb': memory.total // (1024**3),
                'memory_used_gb': memory.used // (1024**3),
                'disk_percent': disk.percent,
                'disk_total_gb': disk.total // (1024**3),
                'disk_free_gb': disk.free // (1024**3),
                'temperature': temp_celsius,
                'uptime': ' '.join(uptime_parts),
                'load_avg': psutil.getloadavg() if hasattr(psutil, 'getloadavg') else (0, 0, 0)
            }
        except Exception as e:
            logger.error(f"Error obteniendo métricas: {e}")
            return None

    @staticmethod
    def get_network_info():
        """Obtener información de red"""
        network_info = {
            'ssid': 'No disponible',
            'ip': 'No disponible', 
            'interface': 'Desconocido'
        }
        
        try:
            # SSID
            result = subprocess.run(['iwgetid', '-r'], capture_output=True, text=True, timeout=3)
            if result.returncode == 0 and result.stdout.strip():
                network_info['ssid'] = result.stdout.strip()
            
            # IP local
            result = subprocess.run(['hostname', '-I'], capture_output=True, text=True, timeout=3)
            if result.returncode == 0:
                ips = result.stdout.strip().split()
                for ip in ips:
                    if ip.startswith(('192.168.', '10.', '172.')):
                        network_info['ip'] = ip
                        break
            
            # Interfaz activa
            result = subprocess.run(['ip', 'route', 'show', 'default'], capture_output=True, text=True, timeout=3)
            if result.returncode == 0:
                parts = result.stdout.split()
                if 'dev' in parts:
                    idx = parts.index('dev')
                    if idx + 1 < len(parts):
                        network_info['interface'] = parts[idx + 1]
        
        except subprocess.TimeoutExpired:
            logger.warning("Timeout obteniendo información de red")
        except Exception as e:
            logger.error(f"Error obteniendo información de red: {e}")
        
        return network_info

    @staticmethod
    def get_service_status(service_name):
        """Verificar estado de servicio"""
        try:
            result = subprocess.run(
                ['systemctl', 'is-active', service_name], 
                capture_output=True, text=True, timeout=5
            )
            return result.stdout.strip() == 'active'
        except Exception:
            return False

    @staticmethod
    def get_hardware_info():
        """Información del hardware"""
        try:
            # Modelo
            model = "Desconocido"
            try:
                with open('/proc/cpuinfo', 'r') as f:
                    for line in f:
                        if 'Model' in line:
                            model = line.split(':', 1)[1].strip()
                            break
            except FileNotFoundError:
                pass
            
            # SO
            os_name = "Linux"
            try:
                with open('/etc/os-release', 'r') as f:
                    for line in f:
                        if line.startswith('PRETTY_NAME='):
                            os_name = line.split('=', 1)[1].strip('"')
                            break
            except FileNotFoundError:
                pass
            
            return {
                'model': model,
                'os': os_name,
                'hostname': subprocess.getoutput("hostname").strip(),
                'kernel': subprocess.getoutput("uname -r").strip()
            }
        except Exception as e:
            logger.error(f"Error obteniendo información de hardware: {e}")
            return {
                'model': 'Desconocido', 
                'os': 'Desconocido', 
                'hostname': 'Desconocido', 
                'kernel': 'Desconocido'
            }

class InfluxDBService:
    """Servicio de consulta a InfluxDB"""
    
    @staticmethod
    def query_sensor_data(sensor_num=None):
        """Consultar datos de sensores desde InfluxDB"""
        if not query_api:
            return None
        
        try:
            if sensor_num:
                measurement = f"corriente{sensor_num}"
                query = f'''
                from(bucket: "{CONFIG['influxdb']['bucket']}")
                |> range(start: -5m)
                |> filter(fn: (r) => r["_measurement"] == "{measurement}")
                |> last()
                |> pivot(rowKey:["_time"], columnKey: ["_field"], valueColumn: "_value")
                '''
            else:
                query = f'''
                from(bucket: "{CONFIG['influxdb']['bucket']}")
                |> range(start: -5m)
                |> filter(fn: (r) => r["_measurement"] == "corriente1" or r["_measurement"] == "corriente2" or r["_measurement"] == "corriente3")
                |> last()
                |> pivot(rowKey:["_time", "_measurement"], columnKey: ["_field"], valueColumn: "_value")
                '''
            
            result = query_api.query(org=CONFIG['influxdb']['org'], query=query)
            return result
            
        except Exception as e:
            logger.error(f"Error consultando datos de sensores: {e}")
            return None

class ResponseFormatter:
    """Formateador de respuestas"""
    
    @staticmethod
    def get_status_indicator(value, warning_threshold, critical_threshold=None):
        """Indicador de estado basado en umbrales"""
        if critical_threshold and value >= critical_threshold:
            return "CRITICO"
        elif value >= warning_threshold:
            return "ADVERTENCIA"
        else:
            return "NORMAL"

    @staticmethod
    def format_service_list(services):
        """Formatear lista de servicios"""
        service_names = {
            'grafana-server': 'Grafana',
            'influxdb': 'InfluxDB',
            'ssh': 'SSH',
            'NetworkManager': 'NetworkManager'
        }
        
        status_lines = []
        for service in services:
            name = service_names.get(service, service)
            status = SystemService.get_service_status(service)
            status_text = "ACTIVO" if status else "INACTIVO"
            status_lines.append(f"{name}: {status_text}")
        
        return '\n'.join(status_lines)

class TechnicalInfo:
    """Información técnica de variables eléctricas"""
    
    VARIABLE_INFO = {
        'voltaje': {
            'title': 'VOLTAJE ELECTRICO (V)',
            'description': 'Diferencia de potencial eléctrico entre dos puntos del circuito',
            'sensor': 'ZMPT101B - Sensor de voltaje AC (medición fase-neutro, hasta 250V AC, calibrado para 120V)',
            'normativa': 'NEC-SB-IE (Ecuador) y IEC 61000-4-30 - Rango nominal 120V con tolerancia ±5% y ±10%',
            'ranges': [
                'SUBTENSION: <108V (Crítico)',
                'LIMITE BAJO: 108-114V (Advertencia)',
                'OPTIMO: 114-126V (Normal)',
                'LIMITE ALTO: 126-132V (Advertencia)',
                'SOBRETENSION: >132V (Crítico)'
            ]
        },
        'corriente': {
            'title': 'CORRIENTE ELECTRICA (A)',
            'description': 'Flujo de carga eléctrica que atraviesa un conductor',
            'sensor': 'SCT013-100A - Transformador de corriente no invasivo (hasta 100A)',
            'normativa': 'NEC-SB-IE - Esquemas de alertas escalonadas (20-100A) según experiencia UNL',
            'ranges': [
                'NORMAL: 0-50A (Operación eficiente en laboratorios y oficinas)',
                'PRECAUCION: 50-80A (Carga elevada, riesgo de sobreuso)',
                'SOBRECARGA: >80A (Crítico, riesgo de daño o disparo de protecciones)'
            ]
        },
        'factor': {
            'title': 'FACTOR DE POTENCIA (cos φ)',
            'description': 'Relación entre potencia real y aparente',
            'sensor': 'Calculado en ESP32 con datos de ZMPT101B y SCT013-100A',
            'normativa': 'NEC-SB-IE y ARCERNNR - Mínimo exigido: 0.92. Multas si <0.85 durante periodos prolongados',
            'ranges': [
                'DEFICIENTE: <0.85 (Requiere corrección inmediata)',
                'ACEPTABLE: 0.85-0.92 (Cumple, pero con riesgo de penalización)',
                'OPTIMO: >0.92 (Eficiencia energética adecuada)'
            ]
        },
        'potencia_real': {
            'title': 'POTENCIA REAL (W)',
            'description': 'Energía activa que realizan las cargas como trabajo útil',
            'sensor': 'Calculada en ESP32: P = V × I × cos φ con ZMPT101B y SCT013-100A',
            'normativa': 'NEC-SB-IE - Máximo recomendable: 80% de la capacidad del circuito',
            'ranges': [
                'EFICIENTE: 0-4.8kW (≤50% capacidad, ejemplo 9.6kW máximo por fase UNL)',
                'PRECAUCION: 4.8-7.7kW (≤80% capacidad)',
                'SOBRECARGA: >7.7kW (Crítico)'
            ]
        },
        'potencia_aparente': {
            'title': 'POTENCIA APARENTE (VA)',
            'description': 'Energía total suministrada al sistema (útil + reactiva)',
            'sensor': 'Calculada en ESP32: S = V × I a partir de los sensores instalados',
            'normativa': 'MIDUVI y NEC-SB-IE - Rango típico en edificios educativos 8-20kVA',
            'ranges': [
                'NORMAL: 0-8kVA (Consumo bajo)',
                'OPERATIVO: 8-15kVA (Rango típico de laboratorios UNL)',
                'ALTO CONSUMO: >15kVA (Advertencia, riesgo de sobrecarga)'
            ]
        }
    }
    
    @classmethod
    def get_variable_info(cls, variable):
        """Obtener información de variable específica"""
        return cls.VARIABLE_INFO.get(variable.lower())

# Funciones auxiliares

async def send_message(update: Update, context: ContextTypes.DEFAULT_TYPE, text: str):
    """Enviar mensaje de forma unificada"""
    try:
        if update.message:
            await update.message.reply_text(text)
        elif update.channel_post:
            await context.bot.send_message(chat_id=update.channel_post.chat_id, text=text)
    except Exception as e:
        logger.error(f"Error enviando mensaje: {e}")

# Comandos del sistema

async def cmd_inicio(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Comando /inicio"""
    text = """SISTEMA ENERGETICO UNL

CONTEXTO ACADEMICO
Este sistema es parte del trabajo de titulación de Tito Xavier Zhanay Avila en la Universidad Nacional de Loja.
Proyecto: "Diseño e implementación de un prototipo portátil de monitoreo de variables eléctricas para las edificaciones de la UNL".
La solución integra sensores no invasivos, un ESP32 y una Raspberry Pi 5 para recopilar y visualizar en tiempo real las principales variables eléctricas de los edificios universitarios, fomentando la eficiencia energética y la sostenibilidad institucional.

CONFIGURACION:
- 3 Sensores de corriente SCT013-100A
- 3 Sensores de voltaje ZMPT101B  
- ESP32 para adquisición y transmisión
- Raspberry Pi 5 para procesamiento
- InfluxDB para almacenamiento
- Grafana para visualización

VARIABLES MONITOREADAS:
Voltaje, Corriente, Potencia Real, Potencia Aparente, Factor de Potencia

Universidad Nacional de Loja
Facultad de Energía, Industrias y Recursos Naturales No Renovables

Utilizar /ayuda para comandos disponibles."""
    
    await send_message(update, context, text)

async def cmd_proyecto(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Comando /proyecto con información académica completa"""
    text = """PROYECTO DE TITULACION UNL

AUTOR: Tito Xavier Zhanay Avila
Universidad Nacional de Loja
Facultad de Energía, Industrias y Recursos Naturales No Renovables

TITULO DEL PROYECTO:
"Diseño e implementación de un prototipo portátil de monitoreo de variables eléctricas para las edificaciones de la Universidad Nacional de Loja"

OBJETIVO GENERAL:
Diseñar e implementar un prototipo portátil de monitoreo de variables eléctricas para las edificaciones de la Universidad Nacional de Loja.

OBJETIVOS ESPECIFICOS:
1. Identificar y seleccionar los parámetros eléctricos necesarios para evaluar la calidad del suministro eléctrico en edificaciones universitarias.

2. Diseñar un prototipo portátil de monitoreo energético que permita la recopilación y transmisión de datos sobre el consumo eléctrico.

3. Implementar y validar el funcionamiento del prototipo portátil en las edificaciones de la Universidad Nacional de Loja.

IMPORTANCIA INSTITUCIONAL:
La solución integra sensores no invasivos, un ESP32 y una Raspberry Pi 5 para recopilar y visualizar en tiempo real las principales variables eléctricas de los edificios universitarios, fomentando la eficiencia energética y la sostenibilidad institucional."""
    
    await send_message(update, context, text)

async def cmd_ayuda(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Comando /ayuda"""
    text = """COMANDOS DISPONIBLES

SISTEMA:
/estado - Dashboard general del sistema
/sistema - Métricas técnicas detalladas  
/salud - Diagnóstico integral
/red - Información de conectividad

DATOS ENERGETICOS:
/datos - Datos de todos los sensores
/sensor1 - Datos específicos sensor 1
/sensor2 - Datos específicos sensor 2
/sensor3 - Datos específicos sensor 3

INFORMACION:
/info <variable> - Información técnica de variables eléctricas
  Variables: voltaje, corriente, factor, potencia_real, potencia_aparente
/proyecto - Información académica del proyecto de titulación

CONTACTO:
/contacto - Información de soporte técnico

Los comandos funcionan en chat privado y en el canal de notificaciones."""
    
    await send_message(update, context, text)

async def cmd_estado(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Comando /estado"""
    metrics = SystemService.get_metrics()
    
    if not metrics:
        await send_message(update, context, "ERROR: No se pueden obtener métricas del sistema")
        return
    
    cpu_status = ResponseFormatter.get_status_indicator(
        metrics['cpu_percent'], CONFIG['thresholds']['cpu_warning']
    )
    memory_status = ResponseFormatter.get_status_indicator(
        metrics['memory_percent'], CONFIG['thresholds']['memory_warning']
    )
    temp_status = ResponseFormatter.get_status_indicator(
        metrics['temperature'], 
        CONFIG['thresholds']['temp_warning'], 
        CONFIG['thresholds']['temp_critical']
    )
    
    services_status = ResponseFormatter.format_service_list(CONFIG['services'])
    
    text = f"""ESTADO DEL SISTEMA

RECURSOS:
CPU: {metrics['cpu_percent']:.2f}% ({cpu_status})
Memoria: {metrics['memory_percent']:.2f}% ({memory_status})
Disco: {metrics['disk_percent']:.2f}%
Temperatura SoC: {metrics['temperature']:.2f}°C ({temp_status})

TIEMPO ACTIVO: {metrics['uptime']}

SERVICIOS:
{services_status}

Estado: OPERATIVO"""
    
    await send_message(update, context, text)

async def cmd_red(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Comando /red"""
    network = SystemService.get_network_info()
    
    # URLs de servicios usando la IP de la Raspberry Pi
    if network['ip'] != 'No disponible':
        grafana_url = f"http://{network['ip']}:3000"
        influxdb_url = f"http://{network['ip']}:8086"
        ssh_access = f"ssh pi@{network['ip']}"
    else:
        grafana_url = "No disponible"
        influxdb_url = "No disponible"
        ssh_access = "No disponible"
    
    # Estado de red
    network_status = "CONECTADO" if network['ip'] != 'No disponible' else "DESCONECTADO"
    nm_status = "ACTIVO" if SystemService.get_service_status('NetworkManager') else "INACTIVO"
    
    text = f"""INFORMACION DE RED

CONECTIVIDAD:
Interfaz: {network['interface']}
SSID: {network['ssid']}
IP Raspberry Pi: {network['ip']}
Estado: {network_status}

ACCESO A SERVICIOS:
Grafana Dashboard: {grafana_url}
InfluxDB: {influxdb_url}
SSH: {ssh_access}

SERVICIOS DE RED:
NetworkManager: {nm_status}

DATOS DE SENSORES:
- Dashboard ESP32: Consultar IP del dispositivo ESP32 en la red local
- Dashboard Grafana: Visualización histórica y en tiempo real
- InfluxDB: Base de datos de series temporales para almacenamiento"""
    
    await send_message(update, context, text)

async def cmd_sistema(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Comando /sistema"""
    metrics = SystemService.get_metrics()
    hardware = SystemService.get_hardware_info()
    
    if not metrics:
        await send_message(update, context, "ERROR: No se puede obtener información del sistema")
        return
    
    text = f"""METRICAS DETALLADAS DEL SISTEMA

HARDWARE:
Modelo: {hardware['model']}
Sistema Operativo: {hardware['os']}
Kernel: {hardware['kernel']}
Hostname: {hardware['hostname']}

RENDIMIENTO:
CPU: {metrics['cpu_percent']:.2f}% ({psutil.cpu_count()} cores)
Load Average: {metrics['load_avg'][0]:.2f} {metrics['load_avg'][1]:.2f} {metrics['load_avg'][2]:.2f}
Memoria: {metrics['memory_used_gb']:.2f}GB/{metrics['memory_total_gb']}GB ({metrics['memory_percent']:.2f}%)
Disco: {metrics['disk_total_gb'] - metrics['disk_free_gb']:.2f}GB/{metrics['disk_total_gb']}GB ({metrics['disk_percent']:.2f}%)

TERMICA:
Temperatura SoC: {metrics['temperature']:.2f}°C
Estado térmico: {ResponseFormatter.get_status_indicator(metrics['temperature'], CONFIG['thresholds']['temp_warning'], CONFIG['thresholds']['temp_critical'])}

TIEMPO DE FUNCIONAMIENTO: {metrics['uptime']}"""
    
    await send_message(update, context, text)

async def cmd_salud(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Comando /salud"""
    metrics = SystemService.get_metrics()
    
    if not metrics:
        await send_message(update, context, "ERROR: Sistema no disponible para diagnóstico")
        return
    
    health_score = 100
    issues = []
    
    # Evaluar métricas
    if metrics['cpu_percent'] > CONFIG['thresholds']['cpu_warning']:
        health_score -= 15
        issues.append(f"CPU elevado: {metrics['cpu_percent']:.2f}%")
    
    if metrics['memory_percent'] > CONFIG['thresholds']['memory_warning']:
        health_score -= 20
        issues.append(f"Memoria alta: {metrics['memory_percent']:.2f}%")
    
    if metrics['disk_percent'] > CONFIG['thresholds']['disk_warning']:
        health_score -= 25
        issues.append(f"Disco lleno: {metrics['disk_percent']:.2f}%")
    
    if metrics['temperature'] > CONFIG['thresholds']['temp_critical']:
        health_score -= 30
        issues.append(f"Temperatura crítica: {metrics['temperature']:.2f}°C")
    elif metrics['temperature'] > CONFIG['thresholds']['temp_warning']:
        health_score -= 10
        issues.append(f"Temperatura alta: {metrics['temperature']:.2f}°C")
    
    # Evaluar servicios
    for service in CONFIG['services']:
        if not SystemService.get_service_status(service):
            health_score -= 20
            issues.append(f"Servicio inactivo: {service}")
    
    # Determinar estado
    if health_score >= 90:
        status = "EXCELENTE"
    elif health_score >= 75:
        status = "BUENO"
    elif health_score >= 50:
        status = "REGULAR"
    else:
        status = "DEFICIENTE"
    
    issues_text = '\n'.join([f"- {issue}" for issue in issues]) if issues else "- Sin problemas detectados"
    
    text = f"""DIAGNOSTICO DE SALUD DEL SISTEMA

PUNTUACION: {health_score}/100
ESTADO GENERAL: {status}

PROBLEMAS DETECTADOS:
{issues_text}

METRICAS ACTUALES:
Tiempo activo: {metrics['uptime']}
Temperatura: {metrics['temperature']:.2f}°C
Memoria: {metrics['memory_percent']:.2f}%
Disco: {metrics['disk_percent']:.2f}%

RECOMENDACION: {'Monitoreo continuo requerido' if health_score < 75 else 'Sistema operando normalmente'}"""
    
    await send_message(update, context, text)

async def cmd_contacto(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Comando /contacto"""
    text = f"""INFORMACION DE CONTACTO

RESPONSABLE DEL PROYECTO:
Nombre: {CONFIG['contact']['name']}
Email: {CONFIG['contact']['email']}

INSTITUCION:
Universidad Nacional de Loja
Facultad de Energía, Industrias y Recursos Naturales No Renovables"""
    
    await send_message(update, context, text)

async def cmd_info(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Comando /info con información técnica"""
    if not context.args:
        text = """INFORMACION TECNICA DE VARIABLES

Uso: /info <variable>

Variables disponibles:
- voltaje: Información técnica del voltaje eléctrico
- corriente: Información de corriente eléctrica
- factor: Factor de potencia
- potencia_real: Potencia real (W)
- potencia_aparente: Potencia aparente (VA)

Ejemplo: /info voltaje"""
        await send_message(update, context, text)
        return
    
    variable = context.args[0].lower()
    info = TechnicalInfo.get_variable_info(variable)
    
    if not info:
        await send_message(update, context, f"ERROR: Variable '{variable}' no reconocida. Utilizar /info para ver variables disponibles.")
        return
    
    ranges_text = '\n'.join([f"- {range_info}" for range_info in info['ranges']])
    
    text = f"""{info['title']}

DEFINICION: {info['description']}
SENSOR: {info['sensor']}
NORMATIVA: {info['normativa']}

RANGOS OPERATIVOS:
{ranges_text}

Esta variable es fundamental para auditorías energéticas y evaluación 
de la calidad del suministro eléctrico en instalaciones universitarias."""
    
    await send_message(update, context, text)

# Comandos de datos energéticos

async def cmd_datos(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Mostrar datos de todos los sensores"""
    try:
        result = InfluxDBService.query_sensor_data()
        
        if not result:
            await send_message(update, context, "ERROR: No se encontraron datos recientes")
            return
        
        sensores_data = {}
        
        # Procesar resultados
        for table in result:
            for record in table.records:
                measurement = record.get_measurement()
                sensor_num = measurement[-1]  # Obtener el número del sensor
                
                if measurement not in sensores_data:
                    sensores_data[measurement] = {
                        'numero': sensor_num,
                        'tiempo': record.get_time()
                    }
                
                # Mapear los campos correctamente
                values = record.values
                for field_key, value in values.items():
                    if field_key.startswith('Voltaje'):
                        sensores_data[measurement]['voltaje'] = value
                    elif field_key.startswith('Corriente'):
                        sensores_data[measurement]['corriente'] = value
                    elif field_key.startswith('PotenciaReal'):
                        sensores_data[measurement]['potencia_real'] = value
                    elif field_key.startswith('PotenciaAparente'):
                        sensores_data[measurement]['potencia_aparente'] = value
                    elif field_key.startswith('FactorPotencia'):
                        sensores_data[measurement]['factor_potencia'] = value
        
        # Construir mensaje
        mensaje = "DATOS ENERGETICOS UNL\n"
        mensaje += "=" * 30 + "\n\n"
        
        total_potencia = 0
        total_corriente = 0
        total_potencia_aparente = 0
        voltajes = []
        factores_potencia = []
        
        for measurement, datos in sorted(sensores_data.items()):
            sensor_num = datos['numero']
            mensaje += f"SENSOR {sensor_num}:\n"
            mensaje += f"  Voltaje: {datos.get('voltaje', 0):.2f} V\n"
            mensaje += f"  Corriente: {datos.get('corriente', 0):.2f} A\n"
            mensaje += f"  Potencia Real: {datos.get('potencia_real', 0):.2f} kW\n"
            mensaje += f"  Potencia Aparente: {datos.get('potencia_aparente', 0):.2f} kVA\n"
            mensaje += f"  Factor Potencia: {datos.get('factor_potencia', 0):.2f}\n\n"
            
            total_potencia += datos.get('potencia_real', 0)
            total_corriente += datos.get('corriente', 0)
            total_potencia_aparente += datos.get('potencia_aparente', 0)
            voltajes.append(datos.get('voltaje', 0))
            factores_potencia.append(datos.get('factor_potencia', 0))
        
        # Resumen total
        voltaje_promedio = sum(voltajes) / len(voltajes) if voltajes else 0
        factor_potencia_promedio = sum(factores_potencia) / len(factores_potencia) if factores_potencia else 0
        
        mensaje += "RESUMEN TOTAL:\n"
        mensaje += f"  Potencia Real Total: {total_potencia:.2f} kW\n"
        mensaje += f"  Potencia Aparente Total: {total_potencia_aparente:.2f} kVA\n"
        mensaje += f"  Corriente Total: {total_corriente:.2f} A\n"
        mensaje += f"  Voltaje Promedio: {voltaje_promedio:.2f} V\n"
        mensaje += f"  Factor Potencia Promedio: {factor_potencia_promedio:.2f}\n"
        
        if sensores_data:
            tiempo = list(sensores_data.values())[0]['tiempo']
            tiempo_local = tiempo.astimezone()
            mensaje += f"  Actualización: {tiempo_local.strftime('%H:%M:%S')}\n"
        
        await send_message(update, context, mensaje)
        
    except Exception as e:
        await send_message(update, context, f"ERROR: Error consultando datos: {str(e)}")

async def cmd_sensor(update: Update, context: ContextTypes.DEFAULT_TYPE, sensor_num):
    """Mostrar datos de un sensor específico"""
    try:
        result = InfluxDBService.query_sensor_data(sensor_num)
        
        if not result or not result[0].records:
            await send_message(update, context, f"ERROR: No se encontraron datos para el sensor {sensor_num}")
            return
        
        record = result[0].records[0]
        values = record.values
        
        mensaje = f"SENSOR {sensor_num} - DATOS DETALLADOS\n"
        mensaje += "=" * 35 + "\n\n"
        
        # Extraer valores según el sensor
        voltaje = 0
        corriente = 0
        potencia_real = 0
        potencia_aparente = 0
        factor_potencia = 0
        
        for field_key, value in values.items():
            if field_key == f'Voltaje{sensor_num}':
                voltaje = value
            elif field_key == f'Corriente{sensor_num}':
                corriente = value
            elif field_key == 'PotenciaReal' and sensor_num == '1':
                potencia_real = value
            elif field_key == f'PotenciaReal{sensor_num}':
                potencia_real = value
            elif field_key == 'PotenciaAparente' and sensor_num == '1':
                potencia_aparente = value
            elif field_key == f'PotenciaAparente{sensor_num}':
                potencia_aparente = value
            elif field_key == f'FactorPotencia{sensor_num}':
                factor_potencia = value
        
        mensaje += f"Voltaje: {voltaje:.2f} V\n"
        mensaje += f"Corriente: {corriente:.2f} A\n"
        mensaje += f"Potencia Real: {potencia_real:.2f} kW\n"
        mensaje += f"Potencia Aparente: {potencia_aparente:.2f} kVA\n"
        mensaje += f"Factor de Potencia: {factor_potencia:.2f}\n\n"
        
        # Información adicional
        if potencia_aparente > 0:
            eficiencia = (potencia_real / potencia_aparente) * 100
            mensaje += f"Eficiencia: {eficiencia:.2f}%\n"
        
        tiempo = record.get_time().astimezone()
        mensaje += f"Última medición: {tiempo.strftime('%d/%m/%Y %H:%M:%S')}\n"
        
        await send_message(update, context, mensaje)
        
    except Exception as e:
        await send_message(update, context, f"ERROR: Error consultando sensor {sensor_num}: {str(e)}")

async def cmd_sensor1(update: Update, context: ContextTypes.DEFAULT_TYPE):
    await cmd_sensor(update, context, '1')

async def cmd_sensor2(update: Update, context: ContextTypes.DEFAULT_TYPE):
    await cmd_sensor(update, context, '2')

async def cmd_sensor3(update: Update, context: ContextTypes.DEFAULT_TYPE):
    await cmd_sensor(update, context, '3')

async def handle_message(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Manejo de mensajes no comando"""
    # Obtener texto del mensaje
    message_text = None
    if update.message and update.message.text:
        message_text = update.message.text.lower().strip()
    elif update.channel_post and update.channel_post.text:
        message_text = update.channel_post.text.lower().strip()
    
    if not message_text or len(message_text) < 2:
        return
    
    # Respuestas básicas
    if any(word in message_text for word in ['estado', 'status']):
        await send_message(update, context, "Sistema operativo. Utilizar /estado para información completa.")
    elif any(word in message_text for word in ['ayuda', 'help', 'comandos']):
        await cmd_ayuda(update, context)
    elif any(word in message_text for word in ['datos', 'sensores']):
        await send_message(update, context, "Utilizar /datos para obtener información de todos los sensores.")
    else:
        await send_message(update, context, "Comando no reconocido. Utilizar /ayuda para ver comandos disponibles.")

async def error_handler(update: Update, context: ContextTypes.DEFAULT_TYPE):
    """Manejo de errores"""
    logger.error(f'Error: {context.error}')

async def setup_commands(application):
    """Configurar comandos del bot"""
    commands = [
        BotCommand("inicio", "Información del sistema"),
        BotCommand("ayuda", "Lista de comandos"),
        BotCommand("estado", "Dashboard del sistema"),
        BotCommand("red", "Información de red"),
        BotCommand("sistema", "Métricas técnicas"),
        BotCommand("salud", "Diagnóstico del sistema"),
        BotCommand("datos", "Datos de todos los sensores"),
        BotCommand("sensor1", "Datos del sensor 1"),
        BotCommand("sensor2", "Datos del sensor 2"),
        BotCommand("sensor3", "Datos del sensor 3"),
        BotCommand("info", "Información de variables"),
        BotCommand("proyecto", "Información del proyecto académico"),
        BotCommand("contacto", "Contacto técnico"),
    ]
    await application.bot.set_my_commands(commands)

def main():
    """Función principal"""
    # Validar configuración
    if not CONFIG['bot_token']:
        logger.error("Token de bot no configurado")
        return
    
    # Crear aplicación
    application = Application.builder().token(CONFIG['bot_token']).build()
    
    # Configurar comandos
    application.job_queue.run_once(lambda _: setup_commands(application), 1)
    
    # Handlers
    handlers = [
        CommandHandler("inicio", cmd_inicio),
        CommandHandler("ayuda", cmd_ayuda),
        CommandHandler("estado", cmd_estado),
        CommandHandler("red", cmd_red),
        CommandHandler("sistema", cmd_sistema),
        CommandHandler("salud", cmd_salud),
        CommandHandler("datos", cmd_datos),
        CommandHandler("sensor1", cmd_sensor1),
        CommandHandler("sensor2", cmd_sensor2),
        CommandHandler("sensor3", cmd_sensor3),
        CommandHandler("info", cmd_info),
        CommandHandler("proyecto", cmd_proyecto),
        CommandHandler("contacto", cmd_contacto),
        MessageHandler(filters.TEXT & ~filters.COMMAND, handle_message),
        MessageHandler(filters.UpdateType.CHANNEL_POST, handle_message),
    ]
    
    for handler in handlers:
        application.add_handler(handler)
    
    application.add_error_handler(error_handler)
    
    # Información de inicio
    logger.info("Bot de Monitoreo Eléctrico UNL iniciado")
    logger.info(f"Responsable: {CONFIG['contact']['name']}")
    logger.info("Comandos disponibles:")
    logger.info("- Sistema: /estado, /red, /sistema, /salud")
    logger.info("- Datos: /datos, /sensor1, /sensor2, /sensor3")
    logger.info("- Info: /info, /proyecto, /contacto")
    
    # Ejecutar
    try:
        application.run_polling(allowed_updates=Update.ALL_TYPES)
    except Exception as e:
        logger.error(f"Error ejecutando bot: {e}")

if __name__ == '__main__':
    main()
