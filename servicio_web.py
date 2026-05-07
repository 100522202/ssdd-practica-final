from spyne import Application, ServiceBase, Unicode, rpc
from spyne.protocol.soap import Soap11
from spyne.server.wsgi import WsgiApplication
from wsgiref.simple_server import make_server
import argparse


# Clase que define el servicio web SOAP
class Normalizador(ServiceBase):

    # Se crea este método como operación remota del servicio web
    # Va a recibir una cadena Unicode y devuelve otra en el mismo formato
    @rpc(Unicode, _returns=Unicode)
    def normalizar(ctx, mensaje):
        # Se separa el mensaje en palabras, eliminando espacios repetidos.
        palabras = mensaje.split()

        # Se vuelve a unir el mensaje dejando un único espacio entre palabras.
        mensaje_normalizado = " ".join(palabras)

        # Se devuelve el mensaje ya normalizado al cliente SOAP.
        return mensaje_normalizado
    
# Se crea la aplicación SOAP con el servicio Normalizador
application = Application(
    # Se indica qué servicios ofrece esta aplicación.
    services=[Normalizador],

    # Se define un namespace propio para identificar el servicio.
    tns="http://ssdd.pfinal.normalizador/",

    # Se indica que las peticiones entrantes usarán SOAP 1.1.
    in_protocol=Soap11(validator="lxml"),

    # Se indica que las respuestas salientes usarán SOAP 1.1.
    out_protocol=Soap11()
)

# Se adapta la aplicación SOAP para poder ejecutarla como aplicación WSGI.
application = WsgiApplication(application)

def main():
    # Se crea el parses para leer argumentos desde la línea de comandos.
    parser = argparse.ArgumentParser()

    # Se permite indicar el puerto del servicio web con -p.
    parser.add_argument("-p", type=int, default=8000, help="Puerto del servicio web")

    # Se leen los argumentos introducidos por consola.
    args = parser.parse_args()

    # Se define la dirección donde escuchará el servicio web.
    server_address = ("127.0.0.1", args.p)

    # Se crea el servidor HTTP que ejecutará la aplicación SOAP.
    server = make_server("127.0.0.1", args.p, application)

    # Se informa por pantalla de la dirección del servicio web.
    print(f"Servicio web escuchando en http://{server_address[0]}:{server_address[1]}")

    # Se informa por pantalla de dónde se puede consultar el WSDL.
    print(f"WSDL disponible en http://{server_address[0]}:{server_address[1]}/?wsdl")
    try:
        # Se deja el servicio web escuchando peticiones indefinidamente.
        server.serve_forever()

    except KeyboardInterrupt: 

        # Se permite terminar el servicio web con Ctrl+C.
        print("\nServicio web terminado")

    finally:
        # Se cierra el servidor al terminar.
        server.server_close()


# Se ejecuta main() solo si este archivo se lanza directamente.
if __name__ == "__main__":
    main()