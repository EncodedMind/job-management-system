.PHONY: all pipes multi-threaded clean help

all: pipes multi-threaded

pipes:
	$(MAKE) -C pipes

multi-threaded:
	$(MAKE) -C multi-threaded

clean:
	$(MAKE) -C pipes clean
	$(MAKE) -C multi-threaded clean

help:
	@echo "Targets:"
	@echo "  make                Build both versions"
	@echo "  make pipes          Build the FIFO-based version"
	@echo "  make multi-threaded Build the TCP multi-threaded version"
	@echo "  make clean          Remove build outputs from both versions"