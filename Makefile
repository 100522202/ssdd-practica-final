# Makefile global por si luego hay que meter más Makefiles aparte del de server.

all:
	$(MAKE) -C src-server

clean:
	$(MAKE) -C src-server clean
	rm -f server

.PHONY: all clean