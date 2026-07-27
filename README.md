# Tesis-de-Grado
Tesis de Grado de Universidad Nacional de Loja
Sistema de Monitoreo de Variables Eléctricas – Repositorio de Recursos

Este repositorio contiene los recursos complementarios del proyecto de monitoreo trifásico de variables eléctricas desarrollado para la tesis en la Universidad Nacional de Loja. Aquí se incluyen los datos recopilados, el diseño de la carcasa impresa en 3D y el circuito impreso (PCB).

📋 Contenido

Datos monitoreados: Archivos Excel con registros de voltaje, corriente, potencia real, potencia aparente y factor de potencia.

Diseño PCB: Archivos del circuito impreso (formato Gerber y esquemático).

Carcasa 3D: Modelos STL y diseños del prototipo impreso.

Diagramas del sistema: Esquemáticos y diagramas de conexión de sensores y microcontrolador.

🔍 Descripción del proyecto

El proyecto consiste en un sistema de monitoreo trifásico que utiliza sensores SCT013 y ZMPT101B conectados a un ESP32, para medir:

Voltaje RMS

Corriente RMS

Potencia real

Potencia aparente

Factor de potencia

Los datos se registran y se exportan a Excel para análisis y generación de reportes. Además, el sistema cuenta con un PCB diseñado para el montaje y una carcasa impresa en 3D para proteger los componentes y facilitar su instalación.

⚡ Características principales

Registro de variables eléctricas en tiempo real

Archivos Excel listos para análisis y gráficas

Diseño de PCB para montaje compacto

Carcasa impresa en 3D para protección y portabilidad

Documentación completa del sistema y conexiones

🛠️ Contenido de los archivos Tipo de archivo Descripción Excel (.xlsx) Datos de monitoreo de cada sensor y medición PCB (.Gerber, .BRD) Diseño del circuito impreso y esquemático Carcasa 3D (.STL) Modelos listos para impresión Diagramas Esquemas de conexión de sensores y microcontrolador 📝 Uso de los datos

Abre los archivos Excel en Microsoft Excel, LibreOffice o Google Sheets.

Cada hoja contiene las lecturas de los sensores (Corriente, Voltaje, Factor de Potencia, Potencia Real y Potencia Aparente).

Puedes graficar tendencias, calcular promedios o analizar picos de consumo.

🖥️ Montaje del hardware

PCB: Utiliza los archivos Gerber para fabricar el circuito impreso.

Sensores: Conecta los SCT013 y ZMPT101B según el esquema del PCB.

Carcasa 3D: Imprime los archivos STL y monta la PCB y los sensores dentro de la carcasa para protección.

📊 Estructura de las carpetas ├── Datos_Excel/ # Archivos de monitoreo en Excel ├── PCB/ # Diseño de PCB y esquemático ├── Carcasa_3D/ # Modelos STL de la carcasa ├── Diagramas/ # Diagramas y esquemas del sistema └── README.md # Documentación del repositorio

🤝 Contribuciones

Si quieres aportar mejoras:

Haz un fork del repositorio

Crea una rama para tu feature

Realiza tus cambios

Envía un pull request

👨‍💻 Autor

Tito Zhanay Universidad Nacional de Loja – 2025

⭐ Si este proyecto te fue útil, considera darle una estrella en GitHub.
