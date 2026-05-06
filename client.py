from enum import Enum
import argparse
import socket
import threading # para el hilo de escucha de connect

class client :

    # ******************** TYPES *********************
    # *
    # * @brief Return codes for the protocol methods
    class RC(Enum) :
        OK = 0
        ERROR = 1
        USER_ERROR = 2

    # ****************** ATTRIBUTES ******************
    _server = None
    _port = -1
    _connected_users = {} # para GETFILE de la parte 2

    # Atributos para el hilo de escucha
    _listen_sock = None
    _is_connected = False

    # Atributo para SEND: El cliente no recibe su propio nombre como 
    # argumento, por tanto es imprescindible que la instancia del 
    # cliente recuerde quién está usando actualmente la interfaz
    # (en el enunciado, 6.4, pone que hay un usuario conectado por interfaz)
    _current_user = None

    # ******************** METHODS *******************

    # Función auxiliar para leer cadenas terminadas en \0 byte a byte
    @staticmethod
    def _read_string(s):
        res = b""
        while True:
            b = s.recv(1)
            if not b or b == b'\x00': # True cuando b es \0
                break
            res += b
        return res.decode('utf-8')


    # Función auxiliar con bucle infinito para procesar todo lo que reciba el cliente
    @staticmethod
    def _listener_thread():
        # Ponemos un timeout al socket para que el hilo pueda evaluar 
        # regularmente si _is_connected ha cambiado a False y cerrarse limpiamente.
        client._listen_sock.settimeout(1.0) 
        
        while client._is_connected:
            try:
                # accept() bloquea hasta que alguien se conecta (o salta el timeout)
                conn, addr = client._listen_sock.accept()
                
                # Leemos qué instrucción nos está mandando el servidor (o el otro cliente)
                op = client._read_string(conn)
                
                if op == "SEND_MESSAGE":
                    remitente = client._read_string(conn)
                    msg_id = client._read_string(conn)
                    mensaje = client._read_string(conn)
                    # El \r evita que se imprima un espacio en blanco no esperado
                    print(f"\rs> MESSAGE {msg_id} FROM {remitente}\n{mensaje}\nEND\nc> ", end="", flush=True)
                    
                elif op == "SEND_MESS_ACK":
                    msg_id = client._read_string(conn)
                    print(f"\rc> SEND MESSAGE {msg_id} OK\nc> ", end="", flush=True)
                    
                elif op == "SEND_MESSAGE_ATTACH":
                    remitente = client._read_string(conn)
                    msg_id = client._read_string(conn)
                    mensaje = client._read_string(conn)
                    fichero = client._read_string(conn)
                    print(f"\rs> MESSAGE {msg_id} FROM {remitente}\n{mensaje}\nEND\nFILE {fichero}\nc> ", end="", flush=True)
                    
                elif op == "SEND_MESS_ATTACH_ACK":
                    msg_id = client._read_string(conn)
                    fichero = client._read_string(conn)
                    print(f"\rc> SENDATTACH MESSAGE {msg_id} {fichero} OK\nc> ", end="", flush=True)
                    
                # TODO: Aquí en el futuro añadiremos el "GET_FILE" de la Parte 2
                
                conn.close()
                
            except socket.timeout:
                # Esto es normal por el timeout de 1 segundo, simplemente iteramos
                continue
            except Exception as e:
                # Si el socket se cierra bruscamente, salimos del bucle
                break


    # *
    # * @param user - User name to register in the system
    # * 
    # * @return OK if successful
    # * @return USER_ERROR if the user is already registered
    # * @return ERROR if another error occurred
    @staticmethod
    def register(user):
        
        # Creación del socket TCP
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        
        # Definición explícita de la tupla de dirección
        server_address = (client._server, client._port)
        
        try:
            # Conexión al servidor
            sock.connect(server_address)
            
            # Envío de la instrucción REGISTER
            op = "REGISTER\0"
            sock.sendall(op.encode('utf-8'))
            
            # Envío del nombre de usuario
            uname = user + "\0"
            sock.sendall(uname.encode('utf-8'))
            
            # Lectura del byte de respuesta
            res = sock.recv(1)
            
            if not res:
                print("c> REGISTER FAIL")
                return client.RC.ERROR
                
            code = int.from_bytes(res, byteorder='little')
            
            if code == 0:
                print("c> REGISTER OK")
                return client.RC.OK
            elif code == 1:
                print("c> USERNAME IN USE")
                return client.RC.USER_ERROR
            else:
                print("c> REGISTER FAIL")
                return client.RC.ERROR
                
        except socket.error as msg:
            # Captura de errores específicos de socket
            print("c> REGISTER FAIL")
            return client.RC.ERROR
            
        finally:
            # Cierre garantizado de la conexión
            sock.close()

    # *
    # 	 * @param user - User name to unregister from the system
    # 	 * 
    # 	 * @return OK if successful
    # 	 * @return USER_ERROR if the user does not exist
    # 	 * @return ERROR if another error occurred
    @staticmethod
    def  unregister(user) :
                
        # Creación del socket TCP
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        
        # Definición explícita de la tupla de dirección
        server_address = (client._server, client._port)
        
        try:
            # Conexión al servidor
            sock.connect(server_address)
            
            # Envío de la instrucción REGISTER
            op = "UNREGISTER\0"
            sock.sendall(op.encode('utf-8'))
            
            # Envío del nombre de usuario
            uname = user + "\0"
            sock.sendall(uname.encode('utf-8'))
            
            # Lectura del byte de respuesta
            res = sock.recv(1)
            
            if not res:
                print("c> UNREGISTER FAIL")
                return client.RC.ERROR
                
            code = int.from_bytes(res, byteorder='little')
            
            if code == 0:
                print("c> UNREGISTER OK")
                return client.RC.OK
            elif code == 1:
                print("c> USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            else:
                print("c> UNREGISTER FAIL")
                return client.RC.ERROR
                
        except socket.error as msg:
            # Captura de errores específicos de socket
            print("c> UNREGISTER FAIL")
            return client.RC.ERROR
            
        finally:
            # Cierre garantizado de la conexión
            sock.close()


    # *
    # * @param user - User name to connect to the system
    # * 
    # * @return OK if successful
    # * @return USER_ERROR if the user does not exist or if it is already connected
    # * @return ERROR if another error occurred
    @staticmethod
    def connect(user):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_address = (client._server, client._port)
        
        try:
            # Crear el socket de escucha para este cliente
            client._listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client._listen_sock.bind(('', 0))
            client._listen_sock.listen(5) # Ponemos el socket en modo escucha: máximo 5 conexiones pendientes
            
            puerto_asignado = client._listen_sock.getsockname()[1]
            
            # Levantar el hilo en segundo plano (daemon=True asegura que muera si cerramos el programa)
            client._is_connected = True
            hilo_escucha = threading.Thread(target=client._listener_thread, daemon=True)
            hilo_escucha.start()

            # Conectar al servidor para notificarle el puerto
            sock.connect(server_address)
            
            sock.sendall("CONNECT\0".encode('utf-8'))
            sock.sendall(f"{user}\0".encode('utf-8'))
            sock.sendall(f"{puerto_asignado}\0".encode('utf-8'))
            
            res = sock.recv(1)
            if not res:
                client._is_connected = False # Abortar hilo
                print("c> CONNECT FAIL")
                return client.RC.ERROR
                
            code = int.from_bytes(res, byteorder='little')
            
            if code == 0:
                client._current_user = user # Guardar el cliente para poder hacer SEND
                print("c> CONNECT OK")
                return client.RC.OK
            else:
                client._is_connected = False # Abortar hilo
                if code == 1 or code == 2:
                    print("c> CONNECT FAIL")
                    return client.RC.USER_ERROR
                else:
                    print("c> CONNECT FAIL")
                    return client.RC.ERROR
                
        except socket.error:
            client._is_connected = False # Abortar hilo
            print("c> CONNECT FAIL")
            return client.RC.ERROR
        finally:
            sock.close()

    # *
    # * 
    # * @return OK if successful
    # * @return USER_ERROR if the user does not exist or if it is already connected
    # * @return ERROR if another error occurred
    @staticmethod
    def users(user) :
        
        # Creación del socket TCP
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_address = (client._server, client._port)
        
        try:
            # Conexión al servidor
            sock.connect(server_address)
            
            # Envío de la instrucción USERS
            sock.sendall("USERS\0".encode('utf-8'))
            
            # Envío del nombre de usuario que hace la petición (Parte 2)
            sock.sendall(f"{user}\0".encode('utf-8'))
            
            # Lectura del byte de respuesta
            res = sock.recv(1)
            
            if not res:
                print("c> CONNECTED USERS FAIL")
                return client.RC.ERROR
                
            code = int.from_bytes(res, byteorder='little')
            
            if code == 0:
                # Usar f. auxiliar para leer bytes 1 a 1
                num_users_str = client._read_string(sock)

                # Solo se convierte si la cadena únicamente tiene dígitos
                num_users = int(num_users_str) if num_users_str.isdigit() else 0
                
                print(f"c> CONNECTED USERS ({num_users} users connected) OK")
                
                client._connected_users.clear() # Limpiamos antes de refrescar
                
                # Leer los datos de cada usuario
                for _ in range(num_users):
                    # En la Parte 2 se lee UNA sola cadena con formato "usuario: IP: puerto"
                    user_info = client._read_string(sock)
                    print(user_info) # Se imprime tal cual viene
                    
                    # Dividir para guardarlo en el diccionario para GETFILE
                    partes = [p.strip() for p in user_info.split('::')]
                    if len(partes) >= 3:
                        uname = partes[0]
                        u_ip = partes[1]
                        u_port = partes[2]
                        client._connected_users[uname] = (u_ip, u_port)
                    
                return client.RC.OK
                
            elif code == 1:
                print("c> CONNECTED USERS FAIL, USER IS NOT CONNECTED")
                return client.RC.USER_ERROR
            else:
                print("c> CONNECTED USERS FAIL")
                return client.RC.ERROR
                
        except socket.error:
            print("c> CONNECTED USERS FAIL")
            return client.RC.ERROR
        finally:
            sock.close()



    # *
    # * @param user - User name to disconnect from the system
    # * 
    # * @return OK if successful
    # * @return USER_ERROR if the user does not exist
    # * @return ERROR if another error occurred
    @staticmethod
    def disconnect(user):
        # Crear socket TCP
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_address = (client._server, client._port)
        
        try:
            sock.connect(server_address)
            
            # Enviar operación DISCONNECT y nombre de usuario (ambos con terminador \0)
            sock.sendall("DISCONNECT\0".encode('utf-8'))
            sock.sendall(f"{user}\0".encode('utf-8'))
            
            # Recibir el byte de respuesta del servidor
            res = sock.recv(1)
            
            # Si no se recibe nada, el servidor cerró la conexión inesperadamente
            if not res:
                print("c> DISCONNECT FAIL")
                return client.RC.ERROR
            
            # Convertir el byte de respuesta a entero
            code = int.from_bytes(res, byteorder='little')
            
            if code == 0:
                # Éxito: marcar como desconectado y cerrar el socket de escucha
                client._is_connected = False
                if client._listen_sock:
                    client._listen_sock.close()
                    
                print("c> DISCONNECT OK")
                return client.RC.OK
            else:
                # Error: código 1 (no existe) o 2 (no estaba conectado)
                if code == 1 or code == 2:
                    print("c> DISCONNECT FAIL")
                    return client.RC.USER_ERROR
                else:
                    # Código 3 otro error genérico
                    print("c> DISCONNECT FAIL")
                    return client.RC.ERROR
                
        except socket.error:
            # Error de red: servidor caído o inalcanzable
            print("c> DISCONNECT FAIL")
            return client.RC.ERROR
        finally:
            sock.close()

    # *
    # * @param user    - Receiver user name
    # * @param message - Message to be sent
    # * 
    # * @return OK if the server had successfully delivered the message
    # * @return USER_ERROR if the user is not connected (the message is queued for delivery)
    # * @return ERROR the user does not exist or another error occurred
    @staticmethod
    def send(user, message) :
        
        # Validar que el cliente haya hecho un CONNECT previo y sepamos quién es
        if not client._current_user:
            print("c> SEND FAIL")
            return client.RC.ERROR
            
        # TODO Parte 2: Hacer aquí la petición HTTP al Servicio Web 
        # local pasándole 'message'. El string que devuelve el servicio web
        # será el nuevo 'message' normalizado que enviaremos por el socket.

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_address = (client._server, client._port)
        
        try:
            sock.connect(server_address)
            
            # Enviar operación SEND
            sock.sendall("SEND\0".encode('utf-8'))
            
            # Enviar nombre del remitente (usuario actual)
            sock.sendall(f"{client._current_user}\0".encode('utf-8'))
            
            # Enviar nombre del destinatario
            sock.sendall(f"{user}\0".encode('utf-8'))
            
            # Enviar el cuerpo del mensaje (máximo 255 caracteres + \0)
            sock.sendall(f"{message}\0".encode('utf-8'))
            
            # Recibir el byte de resultado
            res = sock.recv(1)
            
            if not res:
                print("c> SEND FAIL")
                return client.RC.ERROR
                
            code = int.from_bytes(res, byteorder='little')
            
            if code == 0:
                # En caso de éxito, el servidor devuelve una cadena con el ID asignado
                msg_id = client._read_string(sock)
                print(f"c> SEND OK MESSAGE {msg_id}")
                return client.RC.OK
                
            elif code == 1:
                # Código 1: El usuario destinatario o el remitente no existen
                print("c> SEND FAIL, USER DOES NOT EXIST")
                return client.RC.USER_ERROR
                
            else:
                # Código 2: Cualquier otro error en el servidor
                print("c> SEND FAIL")
                return client.RC.ERROR
                
        except socket.error:
            print("c> SEND FAIL")
            return client.RC.ERROR
        finally:
            sock.close()

    # *
    # * @param user    - Receiver user name
    # * @param file    - file  to be sent
    # * @param message - Message to be sent
    # * 
    # * @return OK if the server had successfully delivered the message
    # * @return USER_ERROR if the user is not connected (the message is queued for delivery)
    # * @return ERROR the user does not exist or another error occurred
    @staticmethod
    def  sendAttach(user,  file,  message) :
        #  Write your code here
        return client.RC.ERROR

    # *
    # **
    # * @brief Command interpreter for the client. It calls the protocol functions.
    @staticmethod
    def shell():

        while (True) :
            try :
                command = input("c> ")
                line = command.split(" ")
                if (len(line) > 0):

                    line[0] = line[0].upper()

                    if (line[0]=="REGISTER") :
                        if (len(line) == 2) :
                            client.register(line[1])
                        else :
                            print("Syntax error. Usage: REGISTER <userName>")

                    elif(line[0]=="UNREGISTER") :
                        if (len(line) == 2) :
                            client.unregister(line[1])
                        else :
                            print("Syntax error. Usage: UNREGISTER <userName>")

                    elif(line[0]=="CONNECT") :
                        if (len(line) == 2) :
                            client.connect(line[1])
                        else :
                            print("Syntax error. Usage: CONNECT <userName>")

                    elif(line[0]=="DISCONNECT") :
                        if (len(line) == 2) :
                            client.disconnect(line[1])
                        else :
                            print("Syntax error. Usage: DISCONNECT <userName>")

                    elif(line[0]=="USERS") :
                        if (len(line) == 2) :
                            client.users(line[1])
                        else :
                            print("Syntax error. Usage: USERS <userName>")

                    elif(line[0]=="SEND") :
                        if (len(line) >= 3) :
                            #  Remove first two words
                            message = ' '.join(line[2:])
                            client.send(line[1], message)
                        else :
                            print("Syntax error. Usage: SEND <userName> <message>")

                    elif(line[0]=="SENDATTACH") :
                        if (len(line) >= 4) :
                            #  Remove first two words
                            message = ' '.join(line[3:])
                            client.sendAttach(line[1], line[2], message)
                        else :
                            print("Syntax error. Usage: SENDATTACH <userName> <filename> <message>")

                    elif(line[0]=="QUIT") :
                        if (len(line) == 1) :
                            break
                        else :
                            print("Syntax error. Use: QUIT")
                    else :
                        print("Error: command " + line[0] + " not valid.")
            except Exception as e:
                print("Exception: " + str(e))

    # *
    # * @brief Prints program usage
    @staticmethod
    def usage() :
        print("Usage: python3 client.py -s <server> -p <port>")


    # *
    # * @brief Parses program execution arguments
    @staticmethod
    def  parseArguments(argv) :
        parser = argparse.ArgumentParser()
        parser.add_argument('-s', type=str, required=True, help='Server IP')
        parser.add_argument('-p', type=int, required=True, help='Server Port')
        args = parser.parse_args()

        if (args.s is None):
            parser.error("Usage: python3 client.py -s <server> -p <port>")
            return False

        if ((args.p < 1024) or (args.p > 65535)):
            parser.error("Error: Port must be in the range 1024 <= port <= 65535");
            return False;
        
        
        client._server = args.s
        client._port = args.p

        return True


    # ******************** MAIN *********************
    @staticmethod
    def main(argv) :
        if (not client.parseArguments(argv)) :
            client.usage()
            return

        #  Write code here
        client.shell()
        print("+++ FINISHED +++")
    

if __name__=="__main__":
    client.main([])
