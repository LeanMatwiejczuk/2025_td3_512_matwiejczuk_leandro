import os, re, sys, termios
from time import sleep

FILE_PATH = "/dev/serial0"  #nombre de mi archivo

def main():
    menu = 0
    #i--> MENU SETCONFIG GETCONFIG START STOP
    msg = ""
    while True:
        match menu:
            #main menu
            case 0:
                match main_menu():
                    case "1":
                        #GOTO env_config
                        menu = 1
                    case "2":
                        #GOTO env_peek
                        menu = 2
                    case "3":
                        #GOTO Start
                        menu = 3
                    case "4":
                        #GOTO Stop
                        menu = 4
                    case "5":
                        print("*****************************************")
                        print("Seguro?:")
                        print("*****************************************")
                        print("1--> Si")
                        print("2--> No")
                        print("*****************************************")
                        print("")
                        match input("[1-2]: ").strip():
                            case "1":
                                os.system("clear")
                                break
                    case _:
                        print("\nOpción invalida")
                        flush_stdin()
                        input("Presione tecla para continuar...")
            #Enviar Config
            case 1:
                temp = env_config()
                match temp:
                    case "1": msg = "Sp"
                    case "2": msg = "ErrSp"
                    case "3": msg = "Mode"
                    case "4": menu=0
                    case _:
                        print("\nOpción invalida")
                        flush_stdin()
                        input("Presione tecla para continuar...")
                
                if temp in ["1","2","3"]:
                    match temp:
                        case "1":
                            print("")
                            print("*****************************************")
                            print("")
                            print("Ingrese Setpoint [0-360]: ")
                            flush_stdin()
                            data = int(input())
                            if 0 <= data <= 360:
                                data_ok = 1
                            else:
                                data_ok = 0
                                print("Dato no válido")
                            msg = f"{msg} {data}"
                        case "2":
                            print("")
                            print("*****************************************")
                            print("")
                            print("Ingrese Error de Setpoint [0-360]: ")
                            flush_stdin()
                            data = int(input())
                            if 0 <= data <= 360:
                                data_ok = 1
                            else:
                                data_ok = 0
                                print("Dato no válido")
                            msg = f"{msg} {data}"
                        case "3":
                            print("")
                            print("*****************************************")
                            print("")
                            print("Ingrese Modo [Rapido / Lento]: ")
                            flush_stdin()
                            data = input()
                            data = data.lower()
                            if data == "rapido" or data == "rápido" or data == "lento":
                                if data == "rapido" or data == "rápido":
                                    data = "00"
                                if data == "lento":
                                    data = "01"
                                data_ok = 1
                            else:
                                data_ok = 0
                                print("Dato no válido")
                            msg = f"{msg} {data}"
                    if(data_ok == 1):
                        print("")
                        print("*****************************************")
                        print(f"Mensaje enviado por UART: {msg}")
                        print("*****************************************")

                        x = uart_send(msg)

                        print(f"{x}")
                        print("*****************************************")
                    else:
                        print("")
                    flush_stdin()
                    input("Presione enter para continuar")
                    menu = 0
            #Get Config
            case 2:
                temp = get_config()
                match temp:
                    case "1": msg = "SpGet"
                    case "2": msg = "ErrSpGet"
                    case "3": msg = "ModeGet"
                    case "4": msg = "Log"
                    case "5": menu=0
                    case _:
                        print("\nOpción invalida")
                        flush_stdin()
                        input("Presione tecla para continuar...")
                
                if temp in ["1","2","3","4"]:
                    temp_get = ""
                    if(temp == "1"): 
                        temp_get = "Setpoint seteado en:"
                    if(temp == "2"): 
                        temp_get = "Error de Setpoint seteado en:"
                    if(temp == "3"): 
                        temp_get = "Modo actual:"
                    if(temp == "4"): 
                        temp_get = "Datos de datalogger:\n"
                    msg = f"{msg}"

                    print("")
                    print("*****************************************")
                    print(f"Mensaje enviado por UART: {msg}")
                    print("*****************************************")

                    x = uart_send(msg)
                    print("*****************************************")
                    print(f"{temp_get} {x}")
                    print("*****************************************")
                    flush_stdin()
                    input("Presione enter para continuar")
                    menu = 0
            #Start
            case 3:
                print("*****************************************")
                print("Iniciando dispositivo...")
                print("*****************************************")
                msg = "Start"
                print("*****************************************")
                print(f"Mensaje enviado por UART: {msg}")
                print("*****************************************")
                
                x = uart_send(msg)
                print("*****************************************")
                print(f"Respuesta: {x}")
                print("*****************************************")
                flush_stdin()
                input("Presione enter para continuar...")
                menu = 0
            #Stop
            case 4:
                print("*****************************************")
                print("Iniciando dispositivo...")
                print("*****************************************")
                msg = "Stop"
                print("*****************************************")
                print(f"Mensaje enviado por UART: {msg}")
                print("*****************************************")
                
                x = uart_send(msg)
                print("*****************************************")
                print(f"Respuesta: {x}")
                print("*****************************************")
                flush_stdin()
                input("Presione enter para continuar...")
                menu = 0
            case _:
                print("\nOpción invalida")
                flush_stdin()
                input("Presione tecla para continuar...")



def uart_send(msg):
    ret = ""
    with open(FILE_PATH, "w") as file:
        file.write(msg + "\n")

    sleep(0.03)        
    with open(FILE_PATH, "r") as file:
        ret = file.read().strip()

    return ret

def uart_init():
    a=0

def main_menu():
    os.system("clear") #envio comando clear
    print("*****************************************")
    print("*********{ User Space UART EGB }*********")
    print("*****************************************")
    print(" ")
    print("Ingrese acción:")
    print(" ")
    print("1--> Enviar configuración")
    print("2--> Recibir información")
    print("3--> Iniciar funcionamiento")
    print("4--> Terminar funcionamiento")
    print("5--> Terminar ejecución")
    print(" ")
    flush_stdin()
    return input("[1-5] :").strip()

def env_config():
    print("*****************************************")
    print("***{ Seleccione variable a configurar }***")
    print("*****************************************")
    print("1--> Setpoint")
    print("2--> Error de Setpoint")
    print("3--> Elegir Modo")
    print("4--> Regresar al menu anterior")
    print("*****************************************")
    print("")
    flush_stdin()
    return input("[1-4] :").strip()

def get_config():
    print("*****************************************")
    print("***{ Seleccione variable a consultar }***")
    print("*****************************************")
    print("1--> Setpoint")
    print("2--> Error de Setpoint")
    print("3--> Modo de funcionamiento")
    print("4--> Log")
    print("5--> Regresar al menu anterior")
    print("*****************************************")
    print("")
    flush_stdin()
    return input("[1-5] :").strip()


def flush_stdin():
    termios.tcflush(sys.stdin, termios.TCIFLUSH)

if __name__ == "__main__" :
    if not os.path.exists(FILE_PATH):
        print(f"Dispositivo {FILE_PATH} no encontrado.")
    else:
        uart_init()
        main()