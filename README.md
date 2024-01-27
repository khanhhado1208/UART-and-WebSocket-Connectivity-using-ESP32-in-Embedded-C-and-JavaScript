# Theoretical Framework   
 UART – UART is an efficient hardware for transmitting data. It converts data stream into bits before transmitting.
 WebSocket – WebSocket is an object that sends and receives data after creating WebSocket connection by providing API. 
 ESP32S3 – ESP32S3 is a microcontroller with integrated Wi-Fi and Bluetooth.

# Research design: 
The integration of the ESP-IDF framework with JavaScript for UART communication and WebSocket control was meticulously designed. 
A combination of agile methodology and collaborative brainstorming sessions provided a flexible and innovative approach to the project.

# Data collection: 
Data acquisition involved the ESP-IDF framework for embedded systems. WebSocket communication was established for remote control, and JavaScript was employed for both remote control functionalities and storing data in the Non-Volatile Storage (NVS). 

# Tool and technologies: 
ESP-IDF Framework: Utilized for programming the ESP32 board, enabling efficient embedded system development. 
JavaScript: JavaScript is employed for fetching data from the ESP32 board (slave board) which contains timer counting data stored in the NVS, as well as for WebSocket communication and remote-control functionalities 
WebSocket: Utilized for real-time bidirectional communication, enabling remote control functionalities. 
NVS (Non-Volatile Storage): Utilized for persistent storage of data on the ESP32 board. 

# Comparison between UART and WebSocket Communication 

## UART Communication: 
Point to point: UART is traditionally point-to-point communication, suitable for direct communication between two devices. 
Physical Connection: Requires a direct physical connection between the devices through UART ports. 
Data Format: Primarily used for serial communication with a specific data format (start bit, data bits, stop bit). 
Speed: Generally, has lower data transfer rates compared to WebSocket. 

## WebSocket Communication: 
Web-Based: WebSocket is a web-based communication protocol, often used for real-time communication over the internet. 
Bi-Directional: Supports bidirectional communication, allowing both the client and server to send messages independently. 
Connection Persistence: WebSocket connections are persistent, reducing the need for constant re-establishment compared to UART. 
Data Format: Allows for flexible data formats, including JSON, making it suitable for various types of data. 



