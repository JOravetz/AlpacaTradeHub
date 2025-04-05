# Makefile for Alpaca API client programs
# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -O2

# Installation directory
B = $(HOME)/bin

# Common library flags
COMMON_LIBS = -lpthread -lm

# Program specific library flags
CURL_LIBS = -lcurl -lssl -lcrypto
SQLITE_LIBS = -lsqlite3

# Program names
PROGRAMS = alpaca_account \
           get_bars \
           trade_fetcher \
           trade_processor

# Define installation targets
INSTALL_TARGETS = $(addprefix $(B)/, $(PROGRAMS))

# Default target - now builds locally
all: $(PROGRAMS)

# Special rule for trade_processor with SQLite
trade_processor: trade_processor.cpp
	$(CXX) $(CXXFLAGS) $< $(COMMON_LIBS) $(SQLITE_LIBS) -o $@
	@chmod 755 $@
	@echo "$@ built in current directory"

# General rule for curl-based programs
alpaca_account get_bars trade_fetcher: %: %.cpp
	$(CXX) $(CXXFLAGS) $< $(COMMON_LIBS) $(CURL_LIBS) -o $@
	@chmod 755 $@
	@echo "$@ built in current directory"

# Installation target
install: $(INSTALL_TARGETS)
	@echo "All programs installed in $(B)"
	@touch $@

# Special installation rule for trade_processor
$(B)/trade_processor: trade_processor.cpp
	@mkdir -p $(B)
	$(CXX) $(CXXFLAGS) $< $(COMMON_LIBS) $(SQLITE_LIBS) -o $@
	@chmod 755 $@
	@echo "$* installed in $(B)"

# General installation rule for curl-based programs
$(B)/alpaca_account $(B)/get_bars $(B)/trade_fetcher: $(B)/%: %.cpp
	@mkdir -p $(B)
	$(CXX) $(CXXFLAGS) $< $(COMMON_LIBS) $(CURL_LIBS) -o $@
	@chmod 755 $@
	@echo "$* installed in $(B)"

# Rebuild everything
remake:
	-touch *.cpp
	-rm -f $(PROGRAMS) $(INSTALL_TARGETS)
	$(MAKE)

# Build in debug mode
debug: CXXFLAGS += -g -DDEBUG
debug: clean all

# Clean build artifacts
clean:
	rm -f $(PROGRAMS) $(INSTALL_TARGETS)
	rm -f a.out core *.o

# Print help information
help:
	@echo "Makefile for Alpaca API client programs"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build all programs in current directory (default)"
	@echo "  install   - Build and install programs in $(B)"
	@echo "  clean     - Remove all built programs"
	@echo "  remake    - Force rebuild of all programs"
	@echo "  debug     - Build with debugging symbols"
	@echo "  help      - Show this help message"

.PHONY: all install clean remake debug help
