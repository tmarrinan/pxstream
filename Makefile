############
MPICXX= mpic++
MPICXX_FLAGS= -std=c++11 -DASIO_STANDALONE -D_VARIADIC_MAX=10 -w

NETSOCKET_DIR= $(HOME)/local
OPENSSL_DIR=/usr/local/opt/openssl
DDR_DIR= $(HOME)/Dev/ddr

# PX STREAM SERVER
INC_S= -I${NETSOCKET_DIR}/include -I$(OPENSSL_DIR)/include -I$(DDR_DIR)/include
LIB_S= -L${NETSOCKET_DIR}/lib -L$(OPENSSL_DIR)/lib -L$(DDR_DIR)/lib -lnetsocket -lddr -lssl -lcrypto
SRCDIR_S= src/server
OBJDIR_S= obj/server
OBJS_S= $(addprefix $(OBJDIR_S)/, pxserver.o)
EXEC_S= $(addprefix $(BINDIR)/, pxserver)

# PX STREAM CLIENT
INC_C= -I${NETSOCKET_DIR}/include -I$(OPENSSL_DIR)/include -I$(DDR_DIR)/include
LIB_C= -L${NETSOCKET_DIR}/lib -L$(OPENSSL_DIR)/lib -L$(DDR_DIR)/lib -lnetsocket -lddr -lssl -lcrypto -lglfw -lglad
SRCDIR_C= src/client
OBJDIR_C= obj/client
OBJS_C= $(addprefix $(OBJDIR_C)/, pxclient.o)
EXEC_C= $(addprefix $(BINDIR)/, pxclient)

BINDIR= bin

# CREATE DIRECTORIES (IF DON'T ALREADY EXIST)
mkdirs:= $(shell mkdir -p $(OBJDIR_S) $(OBJDIR_C) $(BINDIR))

# BUILD EVERYTHING
all: $(EXEC_S) $(EXEC_C)

$(EXEC_S): $(OBJS_S)
	$(MPICXX) $(MPICXX_FLAGS) -o $@ $^ $(LIB_S)

$(OBJDIR_S)/%.o: $(SRCDIR_S)/%.cpp
	$(MPICXX) $(MPICXX_FLAGS) -c -o $@ $< $(INC_S)

$(EXEC_C): $(OBJS_C)
	$(MPICXX) $(MPICXX_FLAGS) -o $@ $^ $(LIB_C)

$(OBJDIR_C)/%.o: $(SRCDIR_C)/%.cpp
	$(MPICXX) $(MPICXX_FLAGS) -c -o $@ $< $(INC_C)

# REMOVE OLD FILES
clean:
	rm -f $(OBJS_S) $(EXEC_S) $(OBJS_C) $(EXEC_C)
