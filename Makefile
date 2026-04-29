all:
	$(MAKE) -C src-server

clean:
	$(MAKE) -C src-server clean
	rm -f server

.PHONY: all clean