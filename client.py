from enum import Enum
import argparse
import socket
import threading # Para el hilo de escucha de connect.
import zeep # Para llamar al servicio web SOAP.

class client :

    # ******************** TYPES *********************.
    # *
    # * @brief Return codes for the protocol methods.
    class RC(Enum) :
        OK = 0
        ERROR = 1
        USER_ERROR = 2

    # ****************** ATTRIBUTES ******************.
    _server = None
    _port = -1
    _connected_users = {} # Para GETFILE de la parte 2.

    # Atributos para el hilo de escucha.
    _listen_sock = None
    _is_connected = False

    # Atributo para SEND: El cliente no recibe su propio nombre como.
    # Argumento, por tanto es imprescindible que la instancia del.
    # Cliente recuerde quién está usando actualmente la interfaz.
    # (en el enunciado, 6.4, pone que hay un usuario conectado por interfaz).
    _current_user = None

    # Cliente SOAP reutilizable para no cargar el WSDL en cada mensaje.
    _soap_client = None
    _wsdl_url = "http://127.0.0.1:8000/?wsdl"

    # ******************** METHODS *******************.

    # Función auxiliar para leer cadenas terminadas en \0 byte a byte.
    @staticmethod
    def _read_string(s):
        res = b""
        while True:
            b = s.recv(1)
            if not b or b == b'\x00': # True cuando b es \0.
                break
            res += b
        return res.decode('utf-8')
    
    # Función auxiliar para recibir exactamente n bytes desde un socket.
    # Se usa para transferir ficheros, ya que su contenido puede incluir cualquier byte y no se puede leer hasta '\0'.
    @staticmethod
    def _recibir_bytes(s, n):
        # Se acumulan bytes hasta recibir exactamente la cantidad esperada.
        data = b""

        # Se sigue leyendo mientras falten bytes por recibir.
        while len(data) < n:
            chunk = s.recv(n - len(data))

            # Si no llega nada, la conexión se ha cerrado antes de tiempo.
            if not chunk:
                return None

            # Se añade el bloque recibido al acumulador.
            data += chunk

        # Se devuelve el contenido completo recibido.
        return data

    # Función auxiliar para normalizar mensajes usando el servicio web SOAP.
    @staticmethod
    def _normalizar_mensaje(message):
        try:
            # Se crea el cliente SOAP usando el WSDL solo la primera vez.
            if client._soap_client is None:
                client._soap_client = zeep.Client(wsdl=client._wsdl_url)

            # Se llama a la operación remota normalizar().
            mensaje_normalizado = client._soap_client.service.normalizar(message)

            # Se devuelve el mensaje recibido desde el servicio web.
            return mensaje_normalizado

        except Exception:
            client._soap_client = None

            # Si el servicio web no responde o hay error, se devuelve None.
            return None

    @staticmethod
    def _stop_listener():
        client._is_connected = False
        client._current_user = None

        if client._listen_sock:
            try:
                client._listen_sock.close()
            except socket.error:
                pass
            client._listen_sock = None


    # Función auxiliar con bucle infinito para procesar todo lo que reciba el cliente.
    @staticmethod
    def _listener_thread():
        listen_sock = client._listen_sock
        if listen_sock is None:
            return

        # Ponemos un timeout al socket para que el hilo pueda evaluar.
        # Regularmente si _is_connected ha cambiado a False y cerrarse limpiamente.
        try:
            listen_sock.settimeout(1.0)
        except socket.error:
            return
        
        while client._is_connected:
            conn = None
            try:
                # Accept() bloquea hasta que alguien se conecta (o salta el timeout).
                conn, addr = listen_sock.accept()
                
                # Leemos qué instrucción nos está mandando el servidor (o el otro cliente).
                op = client._read_string(conn)
                
                if op == "SEND_MESSAGE":
                    remitente = client._read_string(conn)
                    msg_id = client._read_string(conn)
                    mensaje = client._read_string(conn)
                    # El \r evita que se imprima un espacio en blanco no esperado.
                    print(f"\rc> MESSAGE {msg_id} FROM {remitente}\n{mensaje}\nEND\nc> ", end="", flush=True)
                    
                elif op == "SEND_MESS_ACK":
                    msg_id = client._read_string(conn)
                    print(f"\rc> SEND MESSAGE {msg_id} OK\nc> ", end="", flush=True)
                    
                elif op == "SEND_MESSAGE_ATTACH":
                    remitente = client._read_string(conn)
                    msg_id = client._read_string(conn)
                    mensaje = client._read_string(conn)
                    fichero = client._read_string(conn)
                    print(f"\rc> MESSAGE {msg_id} FROM {remitente}\n{mensaje}\nEND\nFILE {fichero}\nc> ", end="", flush=True)
                    
                elif op == "SEND_MESS_ATTACH_ACK":
                    msg_id = client._read_string(conn)
                    fichero = client._read_string(conn)
                    print(f"\rc> SENDATTACH MESSAGE {msg_id} {fichero} OK\nc> ", end="", flush=True)

                elif op == "GET_FILE":
                    # Se lee el usuario que solicita el fichero.
                    solicitante = client._read_string(conn)

                    # Se lee el nombre del fichero solicitado.
                    fichero = client._read_string(conn)

                    try:
                        import os
                        # Se comprueba que el fichero exista y se saca su tamaño para mandarlo por red.
                        # Así evitamos cargar todo en RAM.
                        tamaño = os.path.getsize(fichero)

                        # Se envía primero el tamaño total del fichero como cadena terminada en '\0'.
                        conn.sendall(f"{tamaño}\0".encode("utf-8"))

                        # MODO STREAMING (Lectura por bloques)
                        # Leemos secuencialmente los datos del disco duro en chunks de 4096 bytes
                        # y los inyectamos al socket de red para no sobrecargar la RAM del sistema.
                        with open(fichero, "rb") as f:
                            while True:
                                chunk = f.read(4096)
                                if not chunk: # EOF (Fin del archivo)
                                    break
                                conn.sendall(chunk)

                    except OSError:
                        # Se envía -1 si el fichero no existe o no se puede abrir.
                        conn.sendall("-1\0".encode("utf-8"))
                    
                
            except socket.timeout:
                # Esto es normal por el timeout de 1 segundo, simplemente iteramos.
                continue
            except Exception as e:
                # Si el socket se cierra bruscamente, salimos del bucle.
                break
            finally:
                if conn:
                    conn.close()


    # *
    # * @param user - User name to register in the system.
    # *
    # * @return OK if successful.
    # * @return USER_ERROR if the user is already registered.
    # * @return ERROR if another error occurred.
    @staticmethod
    def register(user):
        
        # Creación del socket TCP.
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        
        # Definición explícita de la tupla de dirección.
        server_address = (client._server, client._port)
        
        try:
            # Conexión al servidor.
            sock.connect(server_address)
            
            # Envío de la instrucción REGISTER.
            op = "REGISTER\0"
            sock.sendall(op.encode('utf-8'))
            
            # Envío del nombre de usuario.
            uname = user + "\0"
            sock.sendall(uname.encode('utf-8'))
            
            # Lectura del byte de respuesta.
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
            # Captura de errores específicos de socket.
            print("c> REGISTER FAIL")
            return client.RC.ERROR
            
        finally:
            # Cierre garantizado de la conexión.
            sock.close()

    # *
    # * @param user - User name to unregister from the system.
    # *
    # * @return OK if successful.
    # * @return USER_ERROR if the user does not exist.
    # * @return ERROR if another error occurred.
    @staticmethod
    def  unregister(user) :
                
        # Creación del socket TCP.
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        
        # Definición explícita de la tupla de dirección.
        server_address = (client._server, client._port)
        
        try:
            # Conexión al servidor.
            sock.connect(server_address)
            
            # Envío de la instrucción UNREGISTER.
            op = "UNREGISTER\0"
            sock.sendall(op.encode('utf-8'))
            
            # Envío del nombre de usuario.
            uname = user + "\0"
            sock.sendall(uname.encode('utf-8'))
            
            # Lectura del byte de respuesta.
            res = sock.recv(1)
            
            if not res:
                print("c> UNREGISTER FAIL")
                return client.RC.ERROR
                
            code = int.from_bytes(res, byteorder='little')
            
            if code == 0:
                print("c> UNREGISTER OK")
                # Si el usuario que borramos es el conectado actual, limpiar la sesión.
                if client._current_user == user:
                    client._stop_listener()
                return client.RC.OK
            elif code == 1:
                print("c> USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            else:
                print("c> UNREGISTER FAIL")
                return client.RC.ERROR
                
        except socket.error as msg:
            # Captura de errores específicos de socket.
            print("c> UNREGISTER FAIL")
            return client.RC.ERROR
            
        finally:
            # Cierre garantizado de la conexión.
            sock.close()


    # *
    # * @param user - User name to connect to the system.
    # *
    # * @return OK if successful.
    # * @return USER_ERROR if the user does not exist or if it is already connected.
    # * @return ERROR if another error occurred.
    @staticmethod
    def connect(user):
        if client._is_connected:
            print("c> USER ALREADY CONNECTED")
            return client.RC.USER_ERROR

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_address = (client._server, client._port)
        
        try:
            # Crear el socket de escucha para este cliente.
            client._listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client._listen_sock.bind(('', 0)) # Puerto asignado por el SO
            client._listen_sock.listen(5) # Ponemos el socket en modo escucha: máximo 5 conexiones pendientes.
            
            puerto_asignado = client._listen_sock.getsockname()[1]
            
            # Levantar el hilo en segundo plano (daemon=True asegura que muera si cerramos el programa).
            client._is_connected = True
            hilo_escucha = threading.Thread(target=client._listener_thread, daemon=True)
            hilo_escucha.start()

            # Conectar al servidor para notificarle el puerto.
            sock.connect(server_address)
            
            sock.sendall("CONNECT\0".encode('utf-8'))
            sock.sendall(f"{user}\0".encode('utf-8'))
            sock.sendall(f"{puerto_asignado}\0".encode('utf-8'))
            
            res = sock.recv(1)
            if not res:
                client._stop_listener()
                print("c> CONNECT FAIL")
                return client.RC.ERROR
                
            code = int.from_bytes(res, byteorder='little')
            
            if code == 0:
                client._current_user = user # Guardar el cliente para poder hacer SEND.
                print("c> CONNECT OK")
                return client.RC.OK
            else:
                client._stop_listener()
                if code == 1:
                    print("c> CONNECT FAIL, USER DOES NOT EXIST")
                    return client.RC.USER_ERROR
                elif code == 2:
                    print("c> USER ALREADY CONNECTED")
                    return client.RC.USER_ERROR
                else:
                    print("c> CONNECT FAIL")
                    return client.RC.ERROR
                
        except socket.error:
            client._stop_listener()
            print("c> CONNECT FAIL")
            return client.RC.ERROR
        finally:
            sock.close()

    # *
    # *
    # * @return OK if successful.
    # * @return USER_ERROR if the user does not exist or if it is already connected.
    # * @return ERROR if another error occurred.
    @staticmethod
    def _refresh_connected_users(mostrar):
        if not client._current_user:
            if mostrar:
                print("c> CONNECTED USERS FAIL, USER IS NOT CONNECTED")
            return client.RC.USER_ERROR

        # Creación del socket TCP.
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_address = (client._server, client._port)
        
        try:
            # Conexión al servidor.
            sock.connect(server_address)
            
            # Envío de la instrucción USERS.
            sock.sendall("USERS\0".encode('utf-8'))
            
            # Envío del nombre de usuario que hace la petición.
            sock.sendall(f"{client._current_user}\0".encode('utf-8'))
            
            # Lectura del byte de respuesta.
            res = sock.recv(1)
            
            if not res:
                if mostrar:
                    print("c> CONNECTED USERS FAIL")
                return client.RC.ERROR
                
            code = int.from_bytes(res, byteorder='little')
            
            if code == 0:
                # Usar f. auxiliar para leer bytes 1 a 1.
                num_users_str = client._read_string(sock)

                # Solo se convierte si la cadena únicamente tiene dígitos.
                num_users = int(num_users_str) if num_users_str.isdigit() else 0
                
                if mostrar:
                    print(f"c> CONNECTED USERS ({num_users} users connected) OK")
                
                client._connected_users.clear() # Limpiamos antes de refrescar.
                
                # Leer los datos de cada usuario.
                for _ in range(num_users):
                    user_info = client._read_string(sock)
                    if mostrar:
                        print(user_info)

                    partes = [p.strip() for p in user_info.split('::')]
                    if len(partes) >= 3:
                        client._connected_users[partes[0]] = (partes[1], partes[2])
                    
                return client.RC.OK
                
            elif code == 1:
                if mostrar:
                    print("c> CONNECTED USERS FAIL, USER IS NOT CONNECTED")
                return client.RC.USER_ERROR
            else:
                if mostrar:
                    print("c> CONNECTED USERS FAIL")
                return client.RC.ERROR
                
        except socket.error:
            if mostrar:
                print("c> CONNECTED USERS FAIL")
            return client.RC.ERROR
        finally:
            sock.close()

    @staticmethod
    def users() :
        return client._refresh_connected_users(True)



    # *
    # * @param user - User name to disconnect from the system.
    # *
    # * @return OK if successful.
    # * @return USER_ERROR if the user does not exist.
    # * @return ERROR if another error occurred.
    @staticmethod
    def disconnect(user):
        if client._current_user and user != client._current_user:
            print("c> DISCONNECT FAIL")
            return client.RC.ERROR

        # Crear socket TCP.
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_address = (client._server, client._port)
        
        try:
            sock.connect(server_address)
            
            # Enviar operación DISCONNECT y nombre de usuario (ambos con terminador \0).
            sock.sendall("DISCONNECT\0".encode('utf-8'))
            sock.sendall(f"{user}\0".encode('utf-8'))
            
            # Recibir el byte de respuesta del servidor.
            res = sock.recv(1)
            
            # Si no se recibe nada, el servidor cerró la conexión inesperadamente.
            if not res:
                client._stop_listener()
                print("c> DISCONNECT FAIL")
                return client.RC.ERROR
            
            # Convertir el byte de respuesta a entero.
            code = int.from_bytes(res, byteorder='little')
            client._stop_listener()
            
            if code == 0:
                print("c> DISCONNECT OK")
                return client.RC.OK
            else:
                if code == 1:
                    print("c> DISCONNECT FAIL, USER DOES NOT EXIST")
                    return client.RC.USER_ERROR
                elif code == 2:
                    print("c> DISCONNECT FAIL, USER NOT CONNECTED")
                    return client.RC.USER_ERROR
                else:
                    # Código 3 otro error genérico.
                    print("c> DISCONNECT FAIL")
                    return client.RC.ERROR
                
        except socket.error:
            # Error de red: servidor caído o inalcanzable.
            client._stop_listener()
            print("c> DISCONNECT FAIL")
            return client.RC.ERROR
        finally:
            sock.close()

    # *
    # * @param user    - Receiver user name.
    # * @param message - Message to be sent.
    # *
    # * @return OK if the server had successfully delivered the message.
    # * @return USER_ERROR if the user is not connected (the message is queued for delivery).
    # * @return ERROR the user does not exist or another error occurred.
    @staticmethod
    def send(user, message) :
        
        # Validar que el cliente haya hecho un CONNECT previo y sepamos quién es.
        if not client._current_user:
            print("c> SEND FAIL")
            return client.RC.ERROR

        # Se normaliza el mensaje usando el servicio web antes de enviarlo al servidor.
        message = client._normalizar_mensaje(message)

        # Si el servicio web falla, se aborta la operación SEND.
        if message is None:
            print("c> SEND FAIL")
            return client.RC.ERROR

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_address = (client._server, client._port)
        
        try:
            sock.connect(server_address)
            
            # Enviar operación SEND.
            sock.sendall("SEND\0".encode('utf-8'))
            
            # Enviar nombre del remitente (usuario actual).
            sock.sendall(f"{client._current_user}\0".encode('utf-8'))
            
            # Enviar nombre del destinatario.
            sock.sendall(f"{user}\0".encode('utf-8'))
            
            # Enviar el cuerpo del mensaje (máximo 255 caracteres + \0).
            sock.sendall(f"{message}\0".encode('utf-8'))
            
            # Recibir el byte de resultado.
            res = sock.recv(1)
            
            if not res:
                print("c> SEND FAIL")
                return client.RC.ERROR
                
            code = int.from_bytes(res, byteorder='little')
            
            if code == 0:
                # En caso de éxito, el servidor devuelve una cadena con el ID asignado.
                msg_id = client._read_string(sock)
                print(f"c> SEND OK - MESSAGE {msg_id}")
                return client.RC.OK
                
            elif code == 1:
                # Código 1: El usuario destinatario o el remitente no existen.
                print("c> SEND FAIL, USER DOES NOT EXIST")
                return client.RC.USER_ERROR
                
            else:
                # Código 2: Cualquier otro error en el servidor.
                print("c> SEND FAIL")
                return client.RC.ERROR
                
        except socket.error:
            print("c> SEND FAIL")
            return client.RC.ERROR
        finally:
            sock.close()

    # *
    # * @param user    - Receiver user name.
    # * @param file    - file  to be sent.
    # * @param message - Message to be sent.
    # *
    # * @return OK if the server had successfully delivered the message.
    # * @return USER_ERROR if the user is not connected (the message is queued for delivery).
    # * @return ERROR the user does not exist or another error occurred.
    @staticmethod
    def sendAttach(user, file, message):
        # Validar que el cliente haya hecho CONNECT previamente.
        # Si no se sabe quién es el usuario actual, no podemos mandar el remitente.
        if not client._current_user:
            print("c> SENDATTACH FAIL")
            return client.RC.ERROR

        # Se normaliza el mensaje usando el servicio web antes de enviarlo al servidor.
        message = client._normalizar_mensaje(message)

        # Si el servicio web falla, se aborta la operación SENDATTACH.
        if message is None:
            print("c> SENDATTACH FAIL")
            return client.RC.ERROR

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_address = (client._server, client._port)

        try:
            # Conectarse al servidor de mensajería.
            sock.connect(server_address)

            # Enviar la operación.
            sock.sendall("SENDATTACH\0".encode('utf-8'))

            # Enviar el usuario remitente, que es el usuario que está conectado.
            sock.sendall(f"{client._current_user}\0".encode('utf-8'))

            # Enviar el usuario destinatario.
            sock.sendall(f"{user}\0".encode('utf-8'))

            # Enviar el texto del mensaje.
            sock.sendall(f"{message}\0".encode('utf-8'))

            # Enviar el nombre o la ruta del fichero.
            sock.sendall(f"{file}\0".encode('utf-8'))

            # Recibir el byte del resultado del servidor.
            res = sock.recv(1)

            if not res:
                print("c> SENDATTACH FAIL")
                return client.RC.ERROR

            code = int.from_bytes(res, byteorder='little')

            if code == 0:
                # Si todo fue bien, el servidor envía después el ID del mensaje como cadena.
                msg_id = client._read_string(sock)
                print(f"c> SENDATTACH OK - MESSAGE {msg_id}")
                return client.RC.OK

            elif code == 1:
                # Alguno de los usuarios no existe.
                print("c> SENDATTACH FAIL, USER DOES NOT EXIST")
                return client.RC.USER_ERROR

            else:
                # Código 2 o cualquier otro caso raro.
                print("c> SENDATTACH FAIL")
                return client.RC.ERROR

        except socket.error:
            print("c> SENDATTACH FAIL")
            return client.RC.ERROR

        finally:
            sock.close()
    
    @staticmethod
    def getFile(user, remote_file, local_file):
        # Se comprueba que haya un usuario conectado en esta interfaz.
        if not client._current_user:
            print("c> FILE TRANSFER FAILED, user not connected.")
            return client.RC.USER_ERROR
        
        # Se busca al usuario remoto en la tabla de usuarios conectados.
        user_data = client._connected_users.get(user)

        # Si no se tiene la IP y el puerto del usuario, se refresca la lista con USERS.
        if user_data is None:
            client._refresh_connected_users(False)
            user_data = client._connected_users.get(user)
        
        # Si después de refrescar sigue sin aparecer, el usuario no está conectado.
        if user_data is None:
            print("c> FILE TRANSFER FAILED, user not connected.")
            return client.RC.USER_ERROR
        
        # Se extraen la IP y el puerto del usuario que tiene el fichero.
        ip_remota, puerto_remoto = user_data

        # Se crea un socket TCP para conectarse directamente al otro cliente.
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        try:
            # Se conecta con el hilo de escucha del usuario remoto.
            sock.connect((ip_remota, int(puerto_remoto)))

            # Se envía la operación de petición del fichero.
            sock.sendall("GET_FILE\0".encode('utf-8'))

            # Se envía el usuario que solicita el fichero.
            sock.sendall(f"{client._current_user}\0".encode('utf-8'))

            # Se envía el nombre del fichero que se quiere obtener.
            sock.sendall(f"{remote_file}\0".encode("utf-8"))

            # El tamaño del fichero se recibe como string.
            size_str = client._read_string(sock)

            # Se comprueba si el otro cliente ha indicado error.
            if not size_str.isdigit():
                print("c> FILE TRANSFER FAILED")
                return client.RC.ERROR
            
            # Se convierte el tamaño recibido a entero.
            size = int(size_str)

            # Se reciben exactamente los bytes que forman el fichero y se guardan
            # leyendo y escribiendo iterativamente sin cargar un buffer unificado temporal.
            bytes_pendientes = size

            with open(local_file, "wb") as f:
                while bytes_pendientes > 0:
                    # En cada iteración leemos como máximo 4KB del socket
                    chunk = sock.recv(min(4096, bytes_pendientes))
                    if not chunk:
                        # Si devuelve nada es que han cortado la conexión (error inexperado)
                        break
                    # Guardamos el bloque al disco reduciendo lo pendiente
                    f.write(chunk)
                    bytes_pendientes -= len(chunk)

            # Se comprueba que no falten bytes (cortes).
            if bytes_pendientes != 0:
                print("c> FILE TRANSFER FAILED")
                # Si falló limpiamos el archivo incompleto para ser rigurosos
                import os
                if os.path.exists(local_file):
                    os.remove(local_file)
                return client.RC.ERROR

            print("c> FILE TRANSFER OK")
            
            # Si se llega hasta aquí es que todo fue correctamente.
            return client.RC.OK

        except (ConnectionRefusedError, socket.timeout):
            print("c> FILE TRANSFER FAILED, user not connected.")
            return client.RC.ERROR

        except (socket.error, OSError, ValueError):
            # Se informa de fallo si hay error de conexión, escritura o conversión.
            print("c> FILE TRANSFER FAILED")
            return client.RC.ERROR

        finally:
            # Se cierra siempre la conexión directa con el cliente remoto.
            sock.close()
    # *
    # **
    # * @brief Command interpreter for the client. It calls the protocol functions.
    @staticmethod
    def shell():

        while (True) :
            try :
                command = input("c> ")
                line = command.split()
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
                        if (len(line) == 1) :
                            client.users()
                        else :
                            print("Syntax error. Usage: USERS")

                    elif(line[0]=="SEND") :
                        if (len(line) >= 3) :
                            # Remove first two words.
                            message = ' '.join(line[2:])
                            client.send(line[1], message)
                        else :
                            print("Syntax error. Usage: SEND <userName> <message>")

                    elif(line[0]=="SENDATTACH") :
                        if (len(line) >= 4) :
                            user = line[1]
                            file = line[-1]
                            message = ' '.join(line[2:-1])
                            client.sendAttach(user, file, message)
                        else :
                            print("Syntax error. Usage: SENDATTACH <userName> <message> <fileName>")
                    
                    elif(line[0]=="GETFILE") :
                        if (len(line) == 4) :
                            client.getFile(line[1], line[2], line[3])
                        else :
                            print("Syntax error. Usage: GETFILE <userName> <fileName> <localFileName>")

                    elif(line[0]=="QUIT") :
                        if (len(line) == 1) :
                            if client._is_connected and client._current_user:
                                client.disconnect(client._current_user)
                            elif client._is_connected:
                                client._stop_listener()
                            break
                        else :
                            print("Syntax error. Use: QUIT")
                    else :
                        print("Error: command " + line[0] + " not valid.")
            except Exception as e:
                print("Exception: " + str(e))

    # *
    # * @brief Prints program usage.
    @staticmethod
    def usage() :
        print("Usage: python3 client.py -s <server> -p <port> [--wsdl-url <url>]")


    # *
    # * @brief Parses program execution arguments.
    @staticmethod
    def  parseArguments(argv) :
        parser = argparse.ArgumentParser()
        parser.add_argument('-s', type=str, required=True, help='Server IP')
        parser.add_argument('-p', type=int, required=True, help='Server Port')
        parser.add_argument('--wsdl-url', type=str, default="http://127.0.0.1:8000/?wsdl",
                            help='SOAP WSDL URL for message normalizer')
        args = parser.parse_args()

        if (args.s is None):
            parser.error("Usage: python3 client.py -s <server> -p <port>")
            return False

        if ((args.p < 1024) or (args.p > 65535)):
            parser.error("Error: Port must be in the range 1024 <= port <= 65535");
            return False;
        
        
        client._server = args.s
        client._port = args.p
        client._wsdl_url = args.wsdl_url

        return True


    # ******************** MAIN *********************.
    @staticmethod
    def main(argv) :
        if (not client.parseArguments(argv)) :
            client.usage()
            return

        # Write code here.
        client.shell()
        print("+++ FINISHED +++")
    

if __name__=="__main__":
    client.main([])
