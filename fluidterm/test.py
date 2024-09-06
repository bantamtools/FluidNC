import serial
import time
import logging
from xmodem import XMODEM
import serial.tools.list_ports
from datetime import datetime

# Set up logging for debugging
logging.basicConfig(level=logging.DEBUG)

# ANSI escape codes for colors
COLOR_RESET = "\033[0m"
COLOR_RED = "\033[91m"
COLOR_GREEN = "\033[92m"
COLOR_WHITE = "\033[97m"  # White for system messages (like XMODEM responses)

# Track the last data type (sent or received) and the need for a new timestamp
last_data_type = None
need_new_timestamp = True

# Function to get serial port list
def list_serial_ports():
    ports = serial.tools.list_ports.comports()
    return [port.device for port in ports]

# Function to print data with color and handle switching between sent/received
def print_data(data, data_type):
    global last_data_type, need_new_timestamp
    timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    
    # Decode the data to a string
    decoded_data = data.decode('ascii', errors='ignore')
    
    # Check if the data is a XMODEM or system response (contains 'ERROR' or 'DEBUG')
    if "ERROR" in decoded_data or "DEBUG" in decoded_data:
        # If it's an XMODEM message, ensure there's a blank line before the message
        if last_data_type != "log":  # Ensure only one blank line between logs and data
            print()  # Add a new line before system messages
        print(f"{COLOR_WHITE}{decoded_data}{COLOR_RESET}", flush=True)
        last_data_type = "log"  # Treat this as a log message type
        need_new_timestamp = True  # Force a new timestamp for next sent/received data
        return  # Don't treat XMODEM messages as regular sent/received data
    
    # If the data type has changed, we need to print a new timestamp
    if last_data_type != data_type:
        print()  # Create a new line when switching between sent and received
        last_data_type = data_type
        need_new_timestamp = True  # A new timestamp is needed after switch
    
    # Print a new timestamp if required (on switch or after newline)
    if need_new_timestamp:
        color = COLOR_GREEN if data_type == "sent" else COLOR_RED
        print(f"{timestamp} {color}[{data_type.upper()}]: ", end="", flush=True)
        need_new_timestamp = False  # Disable new timestamp until the next trigger
    
    # Print the actual data and respect new lines
    print(f"{decoded_data}{COLOR_RESET}", end="", flush=True)

    # Check if the data contains a newline character, reset for the next timestamp
    if '\n' in decoded_data:
        need_new_timestamp = True

# Function to get a single byte from the serial port (received data)
def getc(size, timeout=1):
    data = serial_port.read(size) or None
    if data:
        print_data(data, "received")
    return data

# Function to send a byte to the serial port (sent data)
def putc(data, timeout=1):
    print_data(data, "sent")
    return serial_port.write(data)

# List available serial ports
available_ports = list_serial_ports()

# Prompt user to select a serial port
for i, port in enumerate(available_ports):
    print(f"{i}: {port} - {serial.tools.list_ports.comports()[i].description}")
port_index = input("Select the serial port index (or press Enter to use the first one): ")
port_index = int(port_index) if port_index else 0

# Open the selected serial port
serial_port = serial.Serial(available_ports[port_index], baudrate=115200, timeout=1)
logging.debug(f"Opened serial port: {available_ports[port_index]}")

# Wait a bit for the port to settle
time.sleep(2)

# Send the command to start Xmodem receive on the device
filename = "test_to_plot2.gcode"
command = f"$Xmodem/Receive={filename}\n"
serial_port.write(command.encode('ascii'))
print_data(command.encode('ascii'), "sent")
logging.debug(f"Sent command to receiver: {command.strip()}")

# Monitor the incoming serial data to detect 'CCC'
received_data = b""
while True:
    data = serial_port.read(1)
    if data:
        print_data(data, "received")
        received_data += data
        # Check if 'CCC' is received, indicating the device is ready for file transfer
        if b"CCC" in received_data:
            logging.debug("Receiver is ready, 'CCC' detected.")
            break

# Open the file to be sent
file_path = "/Users/tinkeringtanuki/Downloads/test_to_plot2.gcode"
try:
    with open(file_path, 'rb') as f:
        logging.debug(f"Opened file: {file_path}")
        
        # Set up Xmodem with 128-byte blocks (no 1K blocks)
        modem = XMODEM(getc, putc)
        modem.send(f, retry=16, quiet=False)
        
        logging.debug("Transmission complete.")
except FileNotFoundError:
    logging.error(f"File not found: {file_path}")
except Exception as e:
    logging.error(f"An error occurred: {e}")

# Close the serial port
serial_port.close()
logging.debug(f"Closed serial port: {available_ports[port_index]}")
