import serial
import time

# Podesi svoj port 
SERIAL_PORT = '/dev/ttyACM0' 
BAUD_RATE = 9600

def program_macropad():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2)
        
        print(f"Connected to {SERIAL_PORT}. Waiting 4 secs...")
        time.sleep(4) 
        
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        
        print("Programming started...\n" + "-"*45)
        
        with open('macropad_config.txt', 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                
                # Skip empty lines
                if not line or line.startswith('#'): 
                    continue
                
                command = ""
                
                # If line is valid command, add \n on end
                if (line.startswith("<<") and line.endswith(">>")) or (line.startswith("{{") and line.endswith("}}")):
                    command = line + "\n"
                else:
                    print(f"Skip unrecognized line: {line}")
                    continue
                
                # Sending and reading responses
                if command:
                    print(f"Sending: {line}")
                    ser.write(command.encode('utf-8'))
                    
                    response = ser.readline().decode('utf-8', errors='ignore').strip()
                    
                    if response:
                        print(f"Arduino: ✅ {response}")
                    else:
                        print("Arduino: ❌ No reply (Timeout)")
                    
                    print("-" * 45)
                    time.sleep(0.1) 
        
        print("Programmed successfuly!")
        ser.close()
        
    except FileNotFoundError:
        print("ERROR: File 'macropad_config.txt' not found!")
    except serial.SerialException:
        print(f"ERROR: Cant open port {SERIAL_PORT}. Check port!")
    except Exception as e:
        print(f"UNKNOWN ERROR: {e}")

if __name__ == "__main__":
    program_macropad()
