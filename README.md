# Sistema Distribuido Híbrido de Mensajería y Auditoría - Sistemas Distribuidos (SSDD)

Práctica Final de la asignatura **Sistemas Distribuidos (UC3M)**.

---

## Arquitectura del Sistema

Sistema distribuido multi-servicio que integra varios paradigmas de comunicación:

```
+------------------+         Sockets TCP        +----------------------+
|  Cliente Python  | <========================> | Servidor Central (C) |
|   (client.py)    |                            |    (Multithread)     |
+------------------+                            +----------------------+
        |                                                  |
        | SOAP (WSDL)                                      | ONC RPC
        v                                                  v
+------------------+                            +----------------------+
|  Servicio Web    |                            | Servidor de Logs     |
| (servicio_web.py)|                            |      (RPC en C)      |
+------------------+                            +----------------------+
```

### Componentes:
1. **Servidor Central (C / Sockets TCP + Pthreads):** Gestión de usuarios (registro, conexión, desconexión), buzón de mensajes pendientes y entrega de mensajes en tiempo real.
2. **Servidor de Logs / Auditoría (C / ONC RPC):** Registro distribuido de operaciones mediante llamadas RPC.
3. **Servicio Web Administrativo (Python / SOAP):** Servicio web basado en protocolo SOAP (`spyne`) para operaciones de consulta.
4. **Cliente Interactivo (Python):** Interfaz CLI con hilo de escucha asíncrono para recepción de mensajes en segundo plano.

---

## Despliegue y Ejecución

```bash
# 1. Compilar los módulos en C (Servidor central y RPC)
make

# 2. Iniciar el servicio web SOAP
python servicio_web.py -p 8000

# 3. Iniciar el servidor central
./src-server/server -p 9000

# 4. Iniciar clientes
python client.py -s 127.0.0.1 -p 9000
```
