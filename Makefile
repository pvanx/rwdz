#sesuaiin sama path dpdk lu

DPDK_DIR ?= /usr/local/share/dpdk
CFLAGS += -O3 -march=native -I$(DPDK_DIR)/include
LDFLAGS += -L$(DPDK_DIR)/lib -Wl,-rpath=$(DPDK_DIR)/lib -ldpdk

TARGET = dpdk_multiport_udp_sender
SRCS = src/dpdk_multiport_udp_sender.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)
